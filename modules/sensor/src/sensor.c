#include <sensor/sensor.h>
#include <sensor/events.h>
#include <sensor/provider.h>

#include "sensor_backend.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/slotmap.h>
#include <collection/array.h>
#include <event/event.h>
#include <log/log.h>
#include <thread/mutex.h>
#include <thread/once.h>

#include <string.h>

#define MEL_SENSOR_EVENTS_CAP 256

typedef struct
{
    Mel_Sensor_Provider_Desc desc;
    u32                      generation;
    bool                     active;
} Provider_Entry;

typedef struct
{
    u32                   provider_idx;
    u64                   stable_id;
    Mel_Sensor_Descriptor desc;
    bool                  streaming;
    Mel_Sensor_Reading    latest;
    bool                  have_latest;
} Sensor_Slot;

typedef struct
{
    u64                provider_idx;
    u64                stable_id;
    Mel_SlotMap_Handle handle;
} Reg_Entry;

typedef struct
{
    Mel_Sensor_Raw raw;
    u32            prov;
} Gathered;

typedef struct
{
    bool             initialized;
    const Mel_Alloc* alloc;
    Mel_Executor*    exec;
    Mel_SlotMap      sensors;
    Mel_Array(Provider_Entry) providers;
    Mel_Array(Reg_Entry) registry;
    Mel_Event*    events;
    Mel_Event_Sub poll_sub;
    u32           provider_gen;
} Sensors;

static Sensors   g;
static Mel_Mutex g_lock;
static Mel_Once  g_lock_once = MEL_ONCE_INIT;

static void lock_init(void) { mel_mutex_init(&g_lock, MEL_MUTEX_RECURSIVE); }

static void registry_lock(void)
{
    mel_once(&g_lock_once, lock_init);
    mel_mutex_lock(&g_lock);
}

static void registry_unlock(void) { mel_mutex_unlock(&g_lock); }

static Provider_Entry* provider_get(u32 idx)
{
    if (idx < g.providers.count && g.providers.items[idx].active)
        return &g.providers.items[idx];
    return NULL;
}

static Sensor_Slot* sensor_slot(Mel_SlotMap_Handle h) { return (Sensor_Slot*)mel_slotmap_get(&g.sensors, h); }

static Reg_Entry* reg_find(u32 prov, u64 stable_id)
{
    for (usize i = 0; i < g.registry.count; i++)
        if (g.registry.items[i].provider_idx == prov && g.registry.items[i].stable_id == stable_id)
            return &g.registry.items[i];
    return NULL;
}

static void events_overflow_report(const Mel_Event_Overflow_Info* info, void* user)
{
    MEL_UNUSED(user);
    mel_log_warn("sensor", "event channel full (capacity %u); dropping oldest, total_lagged=%llu", info->ring_capacity, (unsigned long long)info->total_lagged);
}

static void fire_event(Mel_Sensor_Event ev)
{
    if (g.events != NULL)
        mel_event_fire(g.events, &ev);
}

static u32 caps_changed_fields(const Mel_Sensor_Caps* a, const Mel_Sensor_Caps* b)
{
    u32 fields = 0;
    if (a->accel_min_hz != b->accel_min_hz || a->accel_max_hz != b->accel_max_hz || a->gyro_min_hz != b->gyro_min_hz || a->gyro_max_hz != b->gyro_max_hz)
        fields |= MEL_SENSOR_FIELD_RATES;
    if (a->has_accel != b->has_accel || a->has_gyro != b->has_gyro)
        fields |= MEL_SENSOR_FIELD_STREAMS;
    if (a->requires_permission != b->requires_permission)
        fields |= MEL_SENSOR_FIELD_PERMISSION;
    if (a->side != b->side)
        fields |= MEL_SENSOR_FIELD_SIDE;
    return fields;
}

static void on_provider_sample(void* token, const Mel_Sensor_Reading* reading)
{
    Mel_SlotMap_Handle h = mel_slotmap_handle_unpack64((u64)(usize)token);
    registry_lock();
    if (!g.initialized)
    {
        registry_unlock();
        return;
    }
    Sensor_Slot* s = sensor_slot(h);
    if (!s)
    {
        registry_unlock();
        return;
    }
    Mel_Sensor_Reading r = *reading;
    r.sequence = ++s->latest.sequence;
    s->latest = r;
    s->have_latest = true;
    fire_event((Mel_Sensor_Event){ .kind = MEL_SENSOR_EVENT_SAMPLE, .sensor = { h }, .sample = r });
    registry_unlock();
}

static void on_provider_lost(void* token)
{
    Mel_SlotMap_Handle h = mel_slotmap_handle_unpack64((u64)(usize)token);
    registry_lock();
    if (!g.initialized)
    {
        registry_unlock();
        return;
    }
    Sensor_Slot* s = sensor_slot(h);
    if (!s)
    {
        registry_unlock();
        return;
    }
    s->streaming = false;
    registry_unlock();
    mel_log_warn("sensor", "provider reported device loss for sensor {index=%u, gen=%u}", h.index, h.generation);
}

Mel_Sensor_Provider mel_sensor_provider_register(const Mel_Sensor_Provider_Desc* desc)
{
    Provider_Entry e = { .desc = *desc, .generation = ++g.provider_gen, .active = true };
    u32            idx = (u32)g.providers.count;
    mel_array_push(&g.providers, e);
    return (Mel_Sensor_Provider){ .index = idx, .generation = e.generation };
}

void mel_sensor_provider_unregister(Mel_Sensor_Provider p)
{
    if (p.index < g.providers.count && g.providers.items[p.index].generation == p.generation)
        g.providers.items[p.index].active = false;
}

void mel_sensor_init_ex(const Mel_Alloc* alloc, Mel_Executor* exec)
{
    if (g.initialized)
        return;
    g.alloc = alloc ? alloc : mel_alloc_heap();
    g.exec = exec;
    mel_slotmap_init(&g.sensors, g.alloc, .item_size = sizeof(Sensor_Slot), .initial_capacity = 4);
    mel_array_init(&g.providers, g.alloc);
    mel_array_init(&g.registry, g.alloc);
    g.provider_gen = 0;
    g.events = mel_event_create(g.alloc, sizeof(Mel_Sensor_Event), MEL_SENSOR_EVENTS_CAP, mel_event_policy_latest(events_overflow_report, NULL));
    g.poll_sub = g.events != NULL ? mel_event_subscribe_pull(g.events, NULL) : MEL_EVENT_SUB_NULL;
    g.initialized = true;
    mel_sensor__register_host_providers();
    mel_sensor_refresh();
}

void mel_sensor_init(const Mel_Alloc* alloc) { mel_sensor_init_ex(alloc, NULL); }

void mel_sensor_shutdown(void)
{
    if (!g.initialized)
        return;
    registry_lock();
    for (usize i = 0; i < g.registry.count; i++)
    {
        Sensor_Slot*    s = sensor_slot(g.registry.items[i].handle);
        Provider_Entry* prov = provider_get((u32)g.registry.items[i].provider_idx);
        if (s && s->streaming && prov && prov->desc.stop)
            prov->desc.stop(prov->desc.user, g.registry.items[i].stable_id);
        if (prov && prov->desc.close)
            prov->desc.close(prov->desc.user, g.registry.items[i].stable_id);
    }
    g.initialized = false;
    if (g.events != NULL)
        mel_event_unsubscribe(g.events, g.poll_sub);
    mel_event_destroy(g.events);
    mel_array_free(&g.registry);
    mel_array_free(&g.providers);
    mel_slotmap_free(&g.sensors);
    g.events = NULL;
    registry_unlock();
    memset(&g, 0, sizeof g);
}

u32 mel_sensor_refresh(void)
{
    if (!g.initialized)
        mel_sensor_init(NULL);

    registry_lock();

    Mel_Array(Gathered) gs;
    mel_array_init(&gs, g.alloc);

    for (u32 pi = 0; pi < g.providers.count; pi++)
    {
        Provider_Entry* pe = &g.providers.items[pi];
        if (!pe->active || !pe->desc.enumerate)
            continue;
        Mel_Array(Mel_Sensor_Raw) tmp;
        mel_array_init(&tmp, g.alloc);
        mel_array_reserve(&tmp, 8);
        u32 n = pe->desc.enumerate(pe->desc.user, tmp.items, (u32)tmp.capacity);
        while (n > tmp.capacity)
        {
            mel_array_reserve(&tmp, n);
            n = pe->desc.enumerate(pe->desc.user, tmp.items, (u32)tmp.capacity);
        }
        for (u32 i = 0; i < n; i++)
        {
            Gathered gthr = { .raw = tmp.items[i], .prov = pi };
            mel_array_push(&gs, gthr);
        }
        mel_array_free(&tmp);
    }

    Mel_Array(bool) seen;
    mel_array_init(&seen, g.alloc);
    for (usize i = 0; i < g.registry.count; i++)
        mel_array_push(&seen, false);

    for (usize i = 0; i < gs.count; i++)
    {
        Gathered*  gt = &gs.items[i];
        Reg_Entry* e = reg_find(gt->prov, gt->raw.stable_id);
        if (e)
        {
            seen.items[(usize)(e - g.registry.items)] = true;
            Sensor_Slot* s = sensor_slot(e->handle);
            if (s)
            {
                u32 fields = caps_changed_fields(&s->desc.caps, &gt->raw.caps);
                s->desc.name = gt->raw.name;
                s->desc.caps = gt->raw.caps;
                if (fields)
                    fire_event((Mel_Sensor_Event){ .kind = MEL_SENSOR_EVENT_CHANGED, .sensor = { e->handle }, .changed_fields = fields });
            }
            continue;
        }
        Sensor_Slot        slot = { .provider_idx = gt->prov, .stable_id = gt->raw.stable_id, .desc = { .name = gt->raw.name, .caps = gt->raw.caps } };
        Mel_SlotMap_Handle h = mel_slotmap_insert(&g.sensors, &slot);
        Reg_Entry          re = { .stable_id = gt->raw.stable_id, .provider_idx = gt->prov, .handle = h };
        mel_array_push(&g.registry, re);
        mel_array_push(&seen, true);
        fire_event((Mel_Sensor_Event){ .kind = MEL_SENSOR_EVENT_ADDED, .sensor = { h } });
        mel_log_info("sensor", "added: stable_id=%llu accel=%d gyro=%d", (unsigned long long)gt->raw.stable_id, gt->raw.caps.has_accel, gt->raw.caps.has_gyro);
    }

    for (usize i = 0; i < g.registry.count;)
    {
        if (i < seen.count && seen.items[i])
        {
            i++;
            continue;
        }
        Mel_SlotMap_Handle h = g.registry.items[i].handle;
        Sensor_Slot*       s = sensor_slot(h);
        Provider_Entry*    prov = provider_get((u32)g.registry.items[i].provider_idx);
        if (s && s->streaming && prov && prov->desc.stop)
            prov->desc.stop(prov->desc.user, g.registry.items[i].stable_id);
        if (prov && prov->desc.close)
            prov->desc.close(prov->desc.user, g.registry.items[i].stable_id);
        mel_slotmap_remove(&g.sensors, h);
        fire_event((Mel_Sensor_Event){ .kind = MEL_SENSOR_EVENT_REMOVED, .sensor = { h } });

        usize last = g.registry.count - 1;
        g.registry.items[i] = g.registry.items[last];
        if (i < seen.count && last < seen.count)
            seen.items[i] = seen.items[last];
        g.registry.count--;
    }

    mel_array_free(&seen);
    mel_array_free(&gs);
    u32 count = (u32)g.registry.count;
    registry_unlock();
    return count;
}

u32 mel_sensor_count(void) { return g.initialized ? (u32)g.registry.count : 0; }

u32 mel_sensor_list(Mel_Sensor* out, u32 cap)
{
    if (!g.initialized)
        return 0;
    u32 n = g.registry.count < cap ? (u32)g.registry.count : cap;
    for (u32 i = 0; i < n; i++)
        out[i] = (Mel_Sensor){ g.registry.items[i].handle };
    return n;
}

Mel_Sensor_Describe_Result mel_sensor_describe(Mel_Sensor s)
{
    Mel_Sensor_Describe_Result r = { 0 };
    Sensor_Slot*               ss = g.initialized ? sensor_slot(s.h) : NULL;
    if (!ss)
    {
        mel_log_error("sensor", "describe on dead handle {index=%u, gen=%u}", s.h.index, s.h.generation);
        r.status = MEL_SENSOR_ERROR;
        return r;
    }
    r.value = ss->desc;
    r.status = MEL_SENSOR_OK;
    return r;
}

bool mel_sensor_alive(Mel_Sensor s) { return g.initialized && mel_slotmap_alive(&g.sensors, s.h); }

bool mel_sensor_equal(Mel_Sensor a, Mel_Sensor b) { return a.h.index == b.h.index && a.h.generation == b.h.generation; }

Mel_Sensor_Rate_Result mel_sensor_rates(Mel_Sensor s)
{
    Mel_Sensor_Rate_Result r = { 0 };
    Sensor_Slot*           ss = g.initialized ? sensor_slot(s.h) : NULL;
    if (!ss)
    {
        mel_log_error("sensor", "rates on dead handle {index=%u, gen=%u}", s.h.index, s.h.generation);
        r.status = MEL_SENSOR_ERROR;
        return r;
    }
    Provider_Entry* prov = provider_get(ss->provider_idx);
    Mel_Sensor_Caps caps = ss->desc.caps;
    if (prov && prov->desc.query_rates)
    {
        Mel_Sensor_Status st = prov->desc.query_rates(prov->desc.user, ss->stable_id, &caps);
        if (mel_sensor_failed(st))
        {
            r.status = st;
            return r;
        }
        ss->desc.caps = caps;
    }
    r.value = caps;
    r.status = MEL_SENSOR_OK;
    return r;
}

static f32 clamp_rate(f32 want, f32 lo, f32 hi, Mel_Sensor_Status* warn)
{
    if (want <= 0.0f)
        return want;
    if (hi > 0.0f && want > hi)
    {
        *warn |= MEL_SENSOR_WARN_RATE_CLAMPED;
        return hi;
    }
    if (lo > 0.0f && want < lo)
    {
        *warn |= MEL_SENSOR_WARN_RATE_CLAMPED;
        return lo;
    }
    return want;
}

Mel_Sensor_Status mel_sensor_start_opt(Mel_Sensor s, Mel_Sensor_Stream_Opt opt)
{
    if (!g.initialized)
        return MEL_SENSOR_ERROR;
    registry_lock();
    Sensor_Slot* ss = sensor_slot(s.h);
    if (!ss)
    {
        registry_unlock();
        mel_log_error("sensor", "start on dead handle {index=%u, gen=%u}", s.h.index, s.h.generation);
        return MEL_SENSOR_ERROR;
    }
    Provider_Entry* prov = provider_get(ss->provider_idx);
    if (!prov || !prov->desc.start)
    {
        registry_unlock();
        mel_log_error("sensor", "sensor has no start provider");
        return MEL_SENSOR_ERROR | MEL_SENSOR_RESULT_UNAVAILABLE;
    }

    Mel_Sensor_Status        warn = 0;
    Mel_Sensor_Stream_Config cfg = {
        .accel_hz = clamp_rate(opt.accel_hz, ss->desc.caps.accel_min_hz, ss->desc.caps.accel_max_hz, &warn),
        .gyro_hz = clamp_rate(opt.gyro_hz, ss->desc.caps.gyro_min_hz, ss->desc.caps.gyro_max_hz, &warn),
    };
    if (ss->desc.caps.requires_permission)
        warn |= MEL_SENSOR_WARN_PERMISSION_NEEDED;

    Mel_Sensor_Sink sink = {
        .on_sample = on_provider_sample,
        .notify_lost = on_provider_lost,
        .token = (void*)(usize)mel_slotmap_handle_pack64(s.h),
    };
    Mel_Sensor_Status st = prov->desc.start(prov->desc.user, ss->stable_id, &cfg, sink);
    if (mel_sensor_failed(st))
    {
        registry_unlock();
        return st;
    }
    ss->streaming = true;
    fire_event((Mel_Sensor_Event){ .kind = MEL_SENSOR_EVENT_CHANGED, .sensor = { s.h }, .changed_fields = MEL_SENSOR_FIELD_STREAMS });
    registry_unlock();
    return warn ? (warn | MEL_SENSOR_WARNED | (st & ~MEL_SENSOR_SEVERITY_MASK)) : st;
}

Mel_Sensor_Status mel_sensor_stop(Mel_Sensor s)
{
    if (!g.initialized)
        return MEL_SENSOR_ERROR;
    registry_lock();
    Sensor_Slot* ss = sensor_slot(s.h);
    if (!ss)
    {
        registry_unlock();
        mel_log_error("sensor", "stop on dead handle {index=%u, gen=%u}", s.h.index, s.h.generation);
        return MEL_SENSOR_ERROR;
    }
    if (!ss->streaming)
    {
        registry_unlock();
        return MEL_SENSOR_OK | MEL_SENSOR_RESULT_NOT_STREAMING;
    }
    Provider_Entry* prov = provider_get(ss->provider_idx);
    if (prov && prov->desc.stop)
        prov->desc.stop(prov->desc.user, ss->stable_id);
    ss->streaming = false;
    fire_event((Mel_Sensor_Event){ .kind = MEL_SENSOR_EVENT_CHANGED, .sensor = { s.h }, .changed_fields = MEL_SENSOR_FIELD_STREAMS });
    registry_unlock();
    return MEL_SENSOR_OK;
}

bool mel_sensor_streaming(Mel_Sensor s)
{
    Sensor_Slot* ss = g.initialized ? sensor_slot(s.h) : NULL;
    return ss && ss->streaming;
}

Mel_Sensor_Read_Result mel_sensor_read(Mel_Sensor s)
{
    Mel_Sensor_Read_Result r = { 0 };
    if (!g.initialized)
    {
        mel_log_error("sensor", "read before init {index=%u, gen=%u}", s.h.index, s.h.generation);
        r.status = MEL_SENSOR_ERROR;
        return r;
    }
    registry_lock();
    Sensor_Slot* ss = sensor_slot(s.h);
    if (!ss)
    {
        registry_unlock();
        mel_log_error("sensor", "read on dead handle {index=%u, gen=%u}", s.h.index, s.h.generation);
        r.status = MEL_SENSOR_ERROR;
        return r;
    }
    Provider_Entry* prov = provider_get(ss->provider_idx);
    if (prov && prov->desc.read)
    {
        Mel_Sensor_Reading out = { 0 };
        Mel_Sensor_Status  st = prov->desc.read(prov->desc.user, ss->stable_id, &out);
        if (!mel_sensor_failed(st))
        {
            out.sequence = ++ss->latest.sequence;
            ss->latest = out;
            ss->have_latest = true;
            r.value = out;
            r.status = st;
            registry_unlock();
            return r;
        }
        if (!ss->have_latest)
        {
            r.status = st;
            registry_unlock();
            return r;
        }
    }
    if (!ss->have_latest)
    {
        r.status = ss->streaming ? (MEL_SENSOR_OK | MEL_SENSOR_RESULT_NO_DATA) : (MEL_SENSOR_OK | MEL_SENSOR_RESULT_NOT_STREAMING);
        registry_unlock();
        return r;
    }
    r.value = ss->latest;
    r.status = MEL_SENSOR_OK;
    registry_unlock();
    return r;
}

void* mel_sensor_native(Mel_Sensor s)
{
    if (!g.initialized)
        return NULL;
    Sensor_Slot* ss = sensor_slot(s.h);
    if (!ss)
        return NULL;
    Provider_Entry* prov = provider_get(ss->provider_idx);
    return (prov && prov->desc.native) ? prov->desc.native(prov->desc.user, ss->stable_id) : NULL;
}

u32 mel_sensor_poll_events(Mel_Sensor_Event* out, u32 cap)
{
    if (!g.initialized || g.events == NULL)
        return 0;
    u32 n = 0;
    for (; n < cap && mel_event_pull(g.events, g.poll_sub, &out[n]); n++)
        ;
    return n;
}

Mel_Sensor_Subscription mel_sensor_subscribe(Mel_Executor* exec, Mel_Sensor_Event_Callback cb, void* user)
{
    if (!g.initialized || g.events == NULL)
    {
        mel_log_error("sensor", "subscribe before init; no channel");
        return MEL_SENSOR_SUBSCRIPTION_NULL;
    }
    Mel_Executor* target = exec != NULL ? exec : g.exec;
    if (target == NULL)
    {
        mel_log_error("sensor", "subscribe needs an executor; none passed and registry has none (pull-only init)");
        return MEL_SENSOR_SUBSCRIPTION_NULL;
    }
    Mel_Event_Sub sub = mel_event_subscribe_push(g.events, target, (Mel_Event_Callback)cb, user);
    return (Mel_Sensor_Subscription){ sub.handle };
}

void mel_sensor_unsubscribe(Mel_Sensor_Subscription sub)
{
    if (!g.initialized || g.events == NULL)
        return;
    mel_event_unsubscribe(g.events, (Mel_Event_Sub){ sub.handle });
}

bool mel_sensor__stable_id(Mel_Sensor s, u64* out_id)
{
    Sensor_Slot* ss = g.initialized ? sensor_slot(s.h) : NULL;
    if (!ss)
        return false;
    *out_id = ss->stable_id;
    return true;
}
