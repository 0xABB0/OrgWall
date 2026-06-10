#include <vibration/vibration.h>
#include <vibration/provider.h>

#include "vibration_internal.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/slotmap.h>
#include <collection/array.h>
#include <vat/tick.h>
#include <vat/vat.h>
#include <time/nano.h>
#include <log/log.h>

#include <string.h>

#define MEL_VIB_TRANSIENT_FLOOR_S 0.02f

typedef Mel_Vib_Provider_Entry Provider_Entry;
typedef Mel_Vib_Device_Slot    Device_Slot;

typedef struct
{
    u64                stable_id;
    u32                provider_idx;
    Mel_SlotMap_Handle handle;
} Reg_Entry;

typedef struct
{
    Mel_Vib_Device device;
    u32            provider_idx;
    u64            stable_id;
    u64            token;
    Mel_Array(Mel_Vib_Event) events;
    u32                 loop;
    f32                 total_duration_s;
    Mel_Vib_Status      warnings;
    Mel_Vat*            vat;
    Mel_Vib_On_Complete on_complete;
    void*               user;
    Mel_Vat_Tick*       timer;
    u64                 start_ns;
    f32                 elapsed_s;
    bool                paused;
    bool                resolved;
} Playback_Slot;

typedef struct
{
    Mel_Vib_Raw raw;
    u32         prov;
} Gathered;

typedef struct
{
    bool             initialized;
    const Mel_Alloc* alloc;
    Mel_Vat*         vat;
    Mel_SlotMap      devices;
    Mel_SlotMap      playbacks;
    Mel_Array(Provider_Entry) providers;
    Mel_Array(Reg_Entry) registry;
    Mel_Array(Mel_Vib_Playback) active;
    u64 next_token;
    u32 provider_gen;
} Vib;

static Vib g;

static Mel_Vib_Host_Register_Fn g_host_register_override;

void mel_vib__set_host_register(Mel_Vib_Host_Register_Fn fn) { g_host_register_override = fn; }

static u64 now_ns(void) { return mel_nanos_since_unspecified_epoch(); }

static Provider_Entry* provider_get(u32 idx)
{
    if (idx < g.providers.count && g.providers.items[idx].active)
        return &g.providers.items[idx];
    return NULL;
}

static Device_Slot* device_slot(Mel_SlotMap_Handle h) { return (Device_Slot*)mel_slotmap_get(&g.devices, h); }

static Reg_Entry* reg_find(u32 prov, u64 stable_id)
{
    for (usize i = 0; i < g.registry.count; i++)
        if (g.registry.items[i].provider_idx == prov && g.registry.items[i].stable_id == stable_id)
            return &g.registry.items[i];
    return NULL;
}

static void active_remove(Mel_Vib_Playback pb)
{
    for (usize i = 0; i < g.active.count; i++)
    {
        if (g.active.items[i].h.index == pb.h.index && g.active.items[i].h.generation == pb.h.generation)
        {
            g.active.items[i] = g.active.items[g.active.count - 1];
            g.active.count--;
            return;
        }
    }
}

static void resolve(Mel_SlotMap_Handle h, Playback_Slot* ps, Mel_Vib_Status status)
{
    if (ps->resolved)
        return;
    ps->resolved = true;
    Mel_Vib_Playback    pb = { h };
    Mel_Vib_On_Complete cb = ps->on_complete;
    void*               user = ps->user;
    active_remove(pb);
    mel_array_free(&ps->events);
    mel_slotmap_remove(&g.playbacks, h);
    if (cb)
        cb(pb, status, user);
}

static bool on_timer(void* user)
{
    Mel_SlotMap_Handle h = mel_slotmap_handle_unpack64((u64)(usize)user);
    Playback_Slot*     ps = (Playback_Slot*)mel_slotmap_get(&g.playbacks, h);
    if (ps && !ps->resolved && !ps->paused)
    {
        ps->timer = NULL;
        resolve(h, ps, MEL_VIB_OK);
    }
    return false;
}

static void core_notify(void* token, Mel_Vib_Status status)
{
    Mel_SlotMap_Handle h = mel_slotmap_handle_unpack64((u64)(usize)token);
    Playback_Slot*     ps = (Playback_Slot*)mel_slotmap_get(&g.playbacks, h);
    if (ps && !ps->resolved)
    {
        if (ps->timer)
        {
            mel_vat_tick_close(ps->timer);
            ps->timer = NULL;
        }
        resolve(h, ps, status);
    }
}

Mel_Vib_Provider mel_vib_provider_register(const Mel_Vib_Provider_Desc* desc)
{
    Provider_Entry e = { .desc = *desc, .generation = ++g.provider_gen, .active = true };
    u32            idx = (u32)g.providers.count;
    mel_array_push(&g.providers, e);
    return (Mel_Vib_Provider){ .index = idx, .generation = e.generation };
}

void mel_vib_provider_unregister(Mel_Vib_Provider p)
{
    if (p.index < g.providers.count && g.providers.items[p.index].generation == p.generation)
        g.providers.items[p.index].active = false;
}

void mel_vib_init(const Mel_Alloc* alloc, Mel_Vat* vat)
{
    if (g.initialized)
        return;
    g.alloc = alloc ? alloc : mel_alloc_heap();
    g.vat = vat;
    mel_slotmap_init(&g.devices, g.alloc, .item_size = sizeof(Device_Slot), .initial_capacity = 4);
    mel_slotmap_init(&g.playbacks, g.alloc, .item_size = sizeof(Playback_Slot), .initial_capacity = 8);
    mel_array_init(&g.providers, g.alloc);
    mel_array_init(&g.registry, g.alloc);
    mel_array_init(&g.active, g.alloc);
    g.next_token = 0;
    g.provider_gen = 0;
    g.initialized = true;
    if (g_host_register_override)
        g_host_register_override();
    else
        mel_vib__register_host_providers();
    mel_vib_refresh();
}

void mel_vib_shutdown(void)
{
    if (!g.initialized)
        return;
    mel_vib_ff__shutdown();
    while (g.active.count > 0)
        mel_vib_abort(g.active.items[g.active.count - 1]);
    for (usize i = 0; i < g.registry.count; i++)
    {
        Provider_Entry* prov = provider_get(g.registry.items[i].provider_idx);
        if (prov && prov->desc.close)
            prov->desc.close(prov->desc.user, g.registry.items[i].stable_id);
    }
    mel_array_free(&g.registry);
    mel_array_free(&g.providers);
    mel_array_free(&g.active);
    mel_slotmap_free(&g.devices);
    mel_slotmap_free(&g.playbacks);
    memset(&g, 0, sizeof g);
}

u32 mel_vib_refresh(void)
{
    if (!g.initialized)
        return 0;

    Mel_Array(Gathered) gs;
    mel_array_init(&gs, g.alloc);

    for (u32 pi = 0; pi < g.providers.count; pi++)
    {
        Provider_Entry* pe = &g.providers.items[pi];
        if (!pe->active || !pe->desc.enumerate)
            continue;
        Mel_Array(Mel_Vib_Raw) tmp;
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
            Device_Slot* s = device_slot(e->handle);
            if (s)
            {
                s->desc.name = gt->raw.name;
                s->desc.caps = gt->raw.caps;
            }
            continue;
        }
        Device_Slot        slot = { .provider_idx = gt->prov, .stable_id = gt->raw.stable_id, .desc = { .name = gt->raw.name, .caps = gt->raw.caps } };
        Mel_SlotMap_Handle h = mel_slotmap_insert(&g.devices, &slot);
        Reg_Entry          re = { .stable_id = gt->raw.stable_id, .provider_idx = gt->prov, .handle = h };
        mel_array_push(&g.registry, re);
        mel_array_push(&seen, true);
        mel_log_info("vibration", "device added: stable_id=%llu", (unsigned long long)gt->raw.stable_id);
    }

    for (usize i = 0; i < g.registry.count;)
    {
        if (i < seen.count && seen.items[i])
        {
            i++;
            continue;
        }
        Mel_SlotMap_Handle h = g.registry.items[i].handle;
        Mel_Vib_Device     dev = { h };
        for (usize j = 0; j < g.active.count;)
        {
            Mel_Vib_Playback pb = g.active.items[j];
            Playback_Slot*   ps = (Playback_Slot*)mel_slotmap_get(&g.playbacks, pb.h);
            if (ps && mel_vib_equal(ps->device, dev))
            {
                if (ps->timer)
                {
                    mel_vat_tick_close(ps->timer);
                    ps->timer = NULL;
                }
                resolve(pb.h, ps, MEL_VIB_ERROR | MEL_VIB_RESULT_DEVICE_LOST);
                j = 0;
            }
            else
                j++;
        }
        Provider_Entry* prov = provider_get(g.registry.items[i].provider_idx);
        if (prov && prov->desc.close)
            prov->desc.close(prov->desc.user, g.registry.items[i].stable_id);
        mel_slotmap_remove(&g.devices, h);
        usize last = g.registry.count - 1;
        g.registry.items[i] = g.registry.items[last];
        if (i < seen.count && last < seen.count)
            seen.items[i] = seen.items[last];
        g.registry.count--;
    }

    mel_array_free(&seen);
    mel_array_free(&gs);
    return (u32)g.registry.count;
}

u32 mel_vib_count(void) { return g.initialized ? (u32)g.registry.count : 0; }

u32 mel_vib_list(Mel_Vib_Device* out, u32 cap)
{
    if (!g.initialized)
        return 0;
    u32 n = g.registry.count < cap ? (u32)g.registry.count : cap;
    for (u32 i = 0; i < n; i++)
        out[i] = (Mel_Vib_Device){ g.registry.items[i].handle };
    return n;
}

Mel_Vib_Describe_Result mel_vib_describe(Mel_Vib_Device d)
{
    Mel_Vib_Describe_Result r = { 0 };
    Device_Slot*            s = g.initialized ? device_slot(d.h) : NULL;
    if (!s)
    {
        mel_log_error("vibration", "describe on dead device {index=%u, gen=%u}", d.h.index, d.h.generation);
        r.status = MEL_VIB_ERROR;
        return r;
    }
    r.value = s->desc;
    r.status = MEL_VIB_OK;
    return r;
}

bool mel_vib_alive(Mel_Vib_Device d) { return g.initialized && mel_slotmap_alive(&g.devices, d.h); }

bool mel_vib_equal(Mel_Vib_Device a, Mel_Vib_Device b) { return a.h.index == b.h.index && a.h.generation == b.h.generation; }

Mel_Vib_Play_Result mel_vib_play_opt(Mel_Vib_Device d, const Mel_Vib_Pattern* p, Mel_Vib_Play_Opt opt)
{
    Mel_Vib_Play_Result r = { .value = MEL_VIB_PLAYBACK_NULL, .status = MEL_VIB_ERROR };
    if (!g.initialized)
        return r;
    Device_Slot* ds = device_slot(d.h);
    if (!ds)
    {
        mel_log_error("vibration", "play on dead device handle");
        return r;
    }
    Provider_Entry* prov = provider_get(ds->provider_idx);
    if (!prov || !prov->desc.submit)
    {
        mel_log_error("vibration", "device has no submit provider");
        return r;
    }
    if (!p || p->count == 0)
    {
        mel_log_error("vibration", "play with empty pattern");
        return r;
    }

    Mel_Vib_Caps   caps = ds->desc.caps;
    Mel_Vib_Status warn = 0;
    u32            count = p->count;
    if (caps.max_events > 0 && count > caps.max_events)
    {
        count = caps.max_events;
        warn |= MEL_VIB_WARN_PATTERN_TRUNCATED;
    }

    Playback_Slot slot;
    memset(&slot, 0, sizeof slot);
    slot.device = d;
    slot.provider_idx = ds->provider_idx;
    slot.stable_id = ds->stable_id;
    slot.token = ++g.next_token;
    slot.loop = p->loop;
    slot.vat = opt.vat ? opt.vat : g.vat;
    slot.on_complete = opt.on_complete;
    slot.user = opt.user;
    mel_array_init(&slot.events, g.alloc);

    f32 total = 0.0f;
    for (u32 i = 0; i < count; i++)
    {
        Mel_Vib_Event e = p->events[i];
        if (e.intensity_env.count || e.sharpness_env.count)
            warn |= MEL_VIB_WARN_ENVELOPE_BAKED;
        e.intensity_env = (Mel_Vib_Envelope){ 0 };
        e.sharpness_env = (Mel_Vib_Envelope){ 0 };
        if (p->events[i].sharpness > 0.0f && !caps.sharpness)
            warn |= MEL_VIB_WARN_SHARPNESS_DROPPED;
        f32 dur = e.duration > 0.0f ? e.duration : MEL_VIB_TRANSIENT_FLOOR_S;
        f32 end = e.at + dur;
        if (end > total)
            total = end;
        mel_array_push(&slot.events, e);
    }
    if (!caps.amplitude)
        warn |= MEL_VIB_WARN_AMPLITUDE_QUANTIZED;
    if (caps.max_duration_s > 0.0f && total > caps.max_duration_s)
    {
        total = caps.max_duration_s;
        warn |= MEL_VIB_WARN_PATTERN_TRUNCATED;
    }
    slot.total_duration_s = total;

    Mel_SlotMap_Handle h = mel_slotmap_insert(&g.playbacks, &slot);
    Playback_Slot*     ps = (Playback_Slot*)mel_slotmap_get(&g.playbacks, h);

    Mel_Vib_Lowered    lowered = { .events = ps->events.items, .count = (u32)ps->events.count, .total_duration_s = total, .loop = p->loop, .caps = caps };
    Mel_Vib_Completion comp = { .notify = core_notify, .token = (void*)(usize)mel_slotmap_handle_pack64(h) };
    Mel_Vib_Status     sub = prov->desc.submit(prov->desc.user, ds->stable_id, ps->token, &lowered, comp);
    if (mel_vib_failed(sub))
    {
        mel_array_free(&ps->events);
        mel_slotmap_remove(&g.playbacks, h);
        r.status = sub;
        return r;
    }

    ps->start_ns = now_ns();
    if (p->loop != MEL_VIB_LOOP_FOREVER && total > 0.0f)
    {
        warn |= MEL_VIB_WARN_COMPLETION_SYNTHESIZED;
        if (ps->vat)
            ps->timer = mel_vat_tick_open(ps->vat, g.alloc, (i64)((f64)total * 1.0e9), on_timer, comp.token);
    }
    ps->warnings = warn;

    Mel_Vib_Playback pb = { h };
    mel_array_push(&g.active, pb);
    r.value = pb;
    r.status = warn ? (warn | MEL_VIB_WARNED) : MEL_VIB_OK;
    return r;
}

void mel_vib_abort(Mel_Vib_Playback pb)
{
    if (!g.initialized)
        return;
    Playback_Slot* ps = (Playback_Slot*)mel_slotmap_get(&g.playbacks, pb.h);
    if (!ps || ps->resolved)
        return;
    Provider_Entry* prov = provider_get(ps->provider_idx);
    if (ps->timer)
    {
        mel_vat_tick_close(ps->timer);
        ps->timer = NULL;
    }
    if (prov && prov->desc.abort)
        prov->desc.abort(prov->desc.user, ps->stable_id, ps->token);
    resolve(pb.h, ps, MEL_VIB_OK | MEL_VIB_RESULT_ABORTED);
}

void mel_vib_abort_all(Mel_Vib_Device d)
{
    if (!g.initialized)
        return;
    Mel_Array(Mel_Vib_Playback) snap;
    mel_array_init(&snap, g.alloc);
    for (usize i = 0; i < g.active.count; i++)
    {
        Playback_Slot* ps = (Playback_Slot*)mel_slotmap_get(&g.playbacks, g.active.items[i].h);
        if (ps && mel_vib_equal(ps->device, d))
            mel_array_push(&snap, g.active.items[i]);
    }
    for (usize i = 0; i < snap.count; i++)
        mel_vib_abort(snap.items[i]);
    mel_array_free(&snap);
}

Mel_Vib_Status mel_vib_pause(Mel_Vib_Playback pb)
{
    if (!g.initialized)
        return MEL_VIB_ERROR;
    Playback_Slot* ps = (Playback_Slot*)mel_slotmap_get(&g.playbacks, pb.h);
    if (!ps || ps->resolved)
    {
        mel_log_error("vibration", "pause on dead or finished playback");
        return MEL_VIB_ERROR;
    }
    if (ps->paused)
        return MEL_VIB_OK;
    Device_Slot* ds = device_slot(ps->device.h);
    if (!ds || !ds->desc.caps.can_pause)
    {
        mel_log_error("vibration", "device cannot pause");
        return MEL_VIB_ERROR;
    }
    Provider_Entry* prov = provider_get(ps->provider_idx);
    ps->elapsed_s += (f32)((f64)(now_ns() - ps->start_ns) / 1.0e9);
    if (ps->timer)
        mel_vat_tick_pause(ps->timer);
    if (prov && prov->desc.pause)
        prov->desc.pause(prov->desc.user, ps->stable_id, ps->token);
    else if (prov && prov->desc.abort)
        prov->desc.abort(prov->desc.user, ps->stable_id, ps->token);
    ps->paused = true;
    return ds->desc.caps.pause_exact ? MEL_VIB_OK : (MEL_VIB_WARN_PAUSE_QUANTIZED | MEL_VIB_WARNED);
}

Mel_Vib_Status mel_vib_resume(Mel_Vib_Playback pb)
{
    if (!g.initialized)
        return MEL_VIB_ERROR;
    Playback_Slot* ps = (Playback_Slot*)mel_slotmap_get(&g.playbacks, pb.h);
    if (!ps || ps->resolved)
    {
        mel_log_error("vibration", "resume on dead or finished playback");
        return MEL_VIB_ERROR;
    }
    if (!ps->paused)
        return MEL_VIB_OK;
    Device_Slot*    ds = device_slot(ps->device.h);
    Provider_Entry* prov = provider_get(ps->provider_idx);
    if (!ds || !prov)
        return MEL_VIB_ERROR;

    f32 remaining = ps->total_duration_s - ps->elapsed_s;
    if (remaining < 0.0f)
        remaining = 0.0f;
    Mel_Vib_Status warn = 0;

    if (prov->desc.resume)
    {
        prov->desc.resume(prov->desc.user, ps->stable_id, ps->token);
    }
    else if (prov->desc.submit)
    {
        Mel_Array(Mel_Vib_Event) tail;
        mel_array_init(&tail, g.alloc);
        for (usize i = 0; i < ps->events.count; i++)
        {
            Mel_Vib_Event e = ps->events.items[i];
            if (e.at >= ps->elapsed_s)
            {
                e.at -= ps->elapsed_s;
                mel_array_push(&tail, e);
            }
        }
        Mel_Vib_Lowered    lowered = { .events = tail.items, .count = (u32)tail.count, .total_duration_s = remaining, .loop = ps->loop, .caps = ds->desc.caps };
        Mel_Vib_Completion comp = { .notify = core_notify, .token = (void*)(usize)mel_slotmap_handle_pack64(pb.h) };
        prov->desc.submit(prov->desc.user, ps->stable_id, ps->token, &lowered, comp);
        mel_array_free(&tail);
        warn |= MEL_VIB_WARN_PAUSE_QUANTIZED;
    }

    ps->start_ns = now_ns();
    ps->paused = false;
    if (ps->timer && ps->loop != MEL_VIB_LOOP_FOREVER && remaining > 0.0f)
        mel_vat_tick_set_interval(ps->timer, (i64)((f64)remaining * 1.0e9));
    return warn ? (warn | MEL_VIB_WARNED) : MEL_VIB_OK;
}

bool mel_vib_playing(Mel_Vib_Playback pb)
{
    Playback_Slot* ps = g.initialized ? (Playback_Slot*)mel_slotmap_get(&g.playbacks, pb.h) : NULL;
    return ps && !ps->resolved && !ps->paused;
}

bool mel_vib_paused(Mel_Vib_Playback pb)
{
    Playback_Slot* ps = g.initialized ? (Playback_Slot*)mel_slotmap_get(&g.playbacks, pb.h) : NULL;
    return ps && !ps->resolved && ps->paused;
}

void* mel_vib_native(Mel_Vib_Device d)
{
    if (!g.initialized)
        return NULL;
    Device_Slot* ds = device_slot(d.h);
    if (!ds)
        return NULL;
    Provider_Entry* prov = provider_get(ds->provider_idx);
    return (prov && prov->desc.native) ? prov->desc.native(prov->desc.user, ds->stable_id) : NULL;
}

const Mel_Alloc* mel_vib__alloc(void) { return g.alloc; }

bool mel_vib__ready(void) { return g.initialized; }

Mel_Vib_Device_Slot* mel_vib__device_slot(Mel_SlotMap_Handle h) { return g.initialized ? device_slot(h) : NULL; }

Mel_Vib_Provider_Entry* mel_vib__provider(u32 idx) { return g.initialized ? provider_get(idx) : NULL; }

u64 mel_vib__next_token(void) { return ++g.next_token; }
