#include "audioout_internal.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/slotmap.h>
#include <collection/array.h>
#include <executor/executor.h>
#include <event/event.h>
#include <string/str8.h>
#include <log/log.h>

#include <string.h>

#define MEL_AUDIOOUT_HOTPLUG_RING_CAP 64u

typedef struct
{
    Mel_AudioOut_Provider_Desc desc;
    u32                        generation;
    bool                       active;
} Provider_Entry;

typedef struct
{
    u32                      provider_idx;
    str8                     stable_id;
    str8                     name;
    const mel_audioout_kind* kind;
    u32                      channels;
    u32                      samplerate;
    Mel_AudioOut_Rates       rates;
    Mel_AudioOut_Caps        caps;
    f32                      volume;
    bool                     muted;
} Device_Slot;

typedef struct
{
    str8               stable_id;
    u32                provider_idx;
    Mel_SlotMap_Handle handle;
} Reg_Entry;

typedef struct
{
    bool             initialized;
    const Mel_Alloc* alloc;
    Mel_Executor*    exec;

    Mel_SlotMap devices;
    Mel_Array(Provider_Entry) providers;
    Mel_Array(Reg_Entry) registry;

    Mel_Event*         hotplug;
    u32                provider_gen;
    Mel_SlotMap_Handle default_h;
} AudioOut;

static AudioOut g;

static Provider_Entry* provider_get(u32 idx)
{
    if (idx < g.providers.count && g.providers.items[idx].active)
        return &g.providers.items[idx];
    return NULL;
}

static Device_Slot* device_slot(Mel_SlotMap_Handle h) { return (Device_Slot*)mel_slotmap_get(&g.devices, h); }

static Reg_Entry* reg_find(u32 prov, str8 stable_id)
{
    for (usize i = 0; i < g.registry.count; i++)
        if (g.registry.items[i].provider_idx == prov && str8_equals(g.registry.items[i].stable_id, stable_id))
            return &g.registry.items[i];
    return NULL;
}

static void str8_owned_free(str8* s)
{
    if (s->data != NULL)
        mel_dealloc(g.alloc, s->data);
    *s = STR8_EMPTY;
}

static void hotplug_overflow(const Mel_Event_Overflow_Info* info, void* user)
{
    MEL_UNUSED(user);
    mel_log_warn("audioout", "hotplug channel full (capacity %u); total_lagged=%llu", info->ring_capacity, (unsigned long long)info->total_lagged);
}

static void fire_hotplug(Mel_AudioOut_Event ev)
{
    if (g.hotplug != NULL)
        mel_event_fire(g.hotplug, &ev);
}

Mel_AudioOut_Provider mel_audioout_provider_register(const Mel_AudioOut_Provider_Desc* desc)
{
    Provider_Entry e = { .desc = *desc, .generation = ++g.provider_gen, .active = true };
    u32            idx = (u32)g.providers.count;
    mel_array_push(&g.providers, e);
    return (Mel_AudioOut_Provider){ .index = idx, .generation = e.generation };
}

void mel_audioout_provider_unregister(Mel_AudioOut_Provider p)
{
    if (p.index < g.providers.count && g.providers.items[p.index].generation == p.generation)
        g.providers.items[p.index].active = false;
}

static void notify_job(void* data)
{
    MEL_UNUSED(data);
    if (g.initialized)
        mel_audioout_refresh();
}

void mel_audioout_provider_notify(Mel_AudioOut_Provider p)
{
    if (!g.initialized)
        return;
    if (p.index < g.providers.count && g.providers.items[p.index].generation == p.generation)
        mel_executor_call(g.exec, notify_job, NULL, g.alloc);
}

void mel_audioout_init(const Mel_Alloc* alloc, Mel_Executor* deliver)
{
    assert(!g.initialized);
    assert(alloc != NULL);
    if (g.initialized)
        return;
    g.alloc = alloc;
    g.exec = deliver ? deliver : mel_executor_inline();
    mel_slotmap_init(&g.devices, g.alloc, .item_size = sizeof(Device_Slot), .initial_capacity = 4);
    mel_array_init(&g.providers, g.alloc);
    mel_array_init(&g.registry, g.alloc);
    g.provider_gen = 0;
    g.default_h = MEL_SLOTMAP_HANDLE_NULL;
    g.hotplug = mel_event_create(g.alloc, sizeof(Mel_AudioOut_Event), MEL_AUDIOOUT_HOTPLUG_RING_CAP, mel_event_policy_latest(hotplug_overflow, NULL));
    g.initialized = true;
    mel_audioout__register_host_providers();
    mel_audioout__publish_register_provider(g.alloc);
    mel_audioout_refresh();
}

static void device_teardown(Device_Slot* s)
{
    str8_owned_free(&s->stable_id);
    str8_owned_free(&s->name);
    mel_array_free(&s->rates);
}

void mel_audioout_shutdown(void)
{
    assert(g.initialized);
    if (!g.initialized)
        return;

    for (usize i = 0; i < g.registry.count; i++)
    {
        Device_Slot* s = device_slot(g.registry.items[i].handle);
        if (s)
            device_teardown(s);
    }
    for (usize i = 0; i < g.providers.count; i++)
        if (g.providers.items[i].active && g.providers.items[i].desc.shutdown)
            g.providers.items[i].desc.shutdown(g.providers.items[i].desc.user, g.alloc);
    mel_event_destroy(g.hotplug);
    mel_array_free(&g.registry);
    mel_array_free(&g.providers);
    mel_slotmap_free(&g.devices);
    memset(&g, 0, sizeof g);
}

static bool rates_equal(const Mel_AudioOut_Rates* a, const u32* b, u32 b_count)
{
    if (a->count != b_count)
        return false;
    for (u32 i = 0; i < b_count; i++)
        if (a->items[i] != b[i])
            return false;
    return true;
}

static void rates_replace(Mel_AudioOut_Rates* dst, const u32* src, u32 count)
{
    mel_array_clear(dst);
    for (u32 i = 0; i < count; i++)
        mel_array_push(dst, src[i]);
}

static bool device_update(Device_Slot* s, const Mel_AudioOut_Raw* raw)
{
    const mel_audioout_kind* kind = raw->kind ? raw->kind : &mel_audioout_unknown;
    bool                     changed = false;

    if (!str8_equals(s->name, raw->name))
    {
        str8_owned_free(&s->name);
        s->name = str8_dup(raw->name, g.alloc);
        changed = true;
    }
    if (s->kind != kind)
    {
        s->kind = kind;
        changed = true;
    }
    if (s->channels != raw->channels || s->samplerate != raw->samplerate)
    {
        s->channels = raw->channels;
        s->samplerate = raw->samplerate;
        changed = true;
    }
    if (s->caps.volume != raw->caps.volume || s->caps.mute != raw->caps.mute)
    {
        s->caps = raw->caps;
        changed = true;
    }
    if (s->volume != raw->volume || s->muted != raw->muted)
    {
        s->volume = raw->volume;
        s->muted = raw->muted;
        changed = true;
    }
    if (!rates_equal(&s->rates, raw->samplerates, raw->samplerate_count))
    {
        rates_replace(&s->rates, raw->samplerates, raw->samplerate_count);
        changed = true;
    }
    return changed;
}

static Mel_SlotMap_Handle compute_default(void)
{
    for (u32 pi = 0; pi < g.providers.count; pi++)
    {
        Provider_Entry* pe = &g.providers.items[pi];
        if (!pe->active || !pe->desc.default_id)
            continue;
        str8 id = pe->desc.default_id(pe->desc.user);
        if (id.len == 0)
            continue;
        Reg_Entry* e = reg_find(pi, id);
        if (e)
            return e->handle;
    }
    return MEL_SLOTMAP_HANDLE_NULL;
}

typedef Mel_Array(bool) Seen_Flags;

typedef struct
{
    u32         pi;
    Seen_Flags* seen;
} Refresh_Ctx;

static bool refresh_accept(const Mel_AudioOut_Raw* raw, void* user)
{
    Refresh_Ctx* ctx = user;
    u32          pi = ctx->pi;

    Reg_Entry* e = reg_find(pi, raw->stable_id);
    if (e)
    {
        ctx->seen->items[(usize)(e - g.registry.items)] = true;
        Device_Slot* s = device_slot(e->handle);
        if (s && device_update(s, raw))
            fire_hotplug((Mel_AudioOut_Event){ .device = { e->handle }, .kind = s->kind, .changed = true });
        return true;
    }

    Device_Slot slot;
    memset(&slot, 0, sizeof slot);
    slot.provider_idx = pi;
    slot.stable_id = str8_dup(raw->stable_id, g.alloc);
    slot.name = str8_dup(raw->name, g.alloc);
    slot.kind = raw->kind ? raw->kind : &mel_audioout_unknown;
    slot.channels = raw->channels;
    slot.samplerate = raw->samplerate;
    slot.caps = raw->caps;
    slot.volume = raw->volume;
    slot.muted = raw->muted;
    mel_array_init(&slot.rates, g.alloc);
    rates_replace(&slot.rates, raw->samplerates, raw->samplerate_count);

    Mel_SlotMap_Handle h = mel_slotmap_insert(&g.devices, &slot);
    Device_Slot*       inserted = device_slot(h);
    Reg_Entry          re = { .stable_id = inserted->stable_id, .provider_idx = pi, .handle = h };
    mel_array_push(&g.registry, re);
    mel_array_push(ctx->seen, true);
    mel_log_info("audioout", "device added: %.*s [%s] id=%.*s", (int)inserted->name.len, inserted->name.data, mel_audioout_kind_name(inserted->kind), (int)inserted->stable_id.len, inserted->stable_id.data);
    fire_hotplug((Mel_AudioOut_Event){ .device = { h }, .kind = inserted->kind, .added = true });
    return true;
}

u32 mel_audioout_refresh(void)
{
    if (!g.initialized)
        return 0;

    Seen_Flags seen;
    mel_array_init(&seen, g.alloc);
    for (usize i = 0; i < g.registry.count; i++)
        mel_array_push(&seen, false);

    for (u32 pi = 0; pi < g.providers.count; pi++)
    {
        Provider_Entry* pe = &g.providers.items[pi];
        if (!pe->active || !pe->desc.enumerate)
            continue;
        Refresh_Ctx ctx = { .pi = pi, .seen = &seen };
        pe->desc.enumerate(pe->desc.user, refresh_accept, &ctx);
    }

    for (usize i = 0; i < g.registry.count;)
    {
        if (i < seen.count && seen.items[i])
        {
            i++;
            continue;
        }
        Mel_SlotMap_Handle       h = g.registry.items[i].handle;
        Device_Slot*             s = device_slot(h);
        const mel_audioout_kind* kind = s ? s->kind : &mel_audioout_unknown;
        if (s)
        {
            mel_log_info("audioout", "device removed: %.*s", (int)s->name.len, s->name.data);
            device_teardown(s);
        }
        mel_slotmap_remove(&g.devices, h);
        fire_hotplug((Mel_AudioOut_Event){ .device = { h }, .kind = kind, .removed = true });

        usize last = g.registry.count - 1;
        g.registry.items[i] = g.registry.items[last];
        if (i < seen.count && last < seen.count)
            seen.items[i] = seen.items[last];
        g.registry.count--;
    }

    mel_array_free(&seen);

    Mel_SlotMap_Handle def = compute_default();
    if (def.index != g.default_h.index || def.generation != g.default_h.generation)
    {
        g.default_h = def;
        Device_Slot* s = device_slot(def);
        fire_hotplug((Mel_AudioOut_Event){ .device = { def }, .kind = s ? s->kind : &mel_audioout_unknown, .default_changed = true });
    }

    if (g.registry.count == 0)
        mel_log_warn("audioout", "zero output devices enumerated across %u provider(s)", (u32)g.providers.count);

    return (u32)g.registry.count;
}

u32 mel_audioout_count(void) { return g.initialized ? (u32)g.registry.count : 0; }

u32 mel_audioout_list(Mel_AudioOut* out, u32 cap)
{
    if (!g.initialized)
        return 0;
    u32 n = g.registry.count < cap ? (u32)g.registry.count : cap;
    for (u32 i = 0; i < n; i++)
        out[i] = (Mel_AudioOut){ g.registry.items[i].handle };
    return n;
}

Mel_AudioOut mel_audioout_default(void)
{
    if (!g.initialized)
        return MEL_AUDIOOUT_NULL;
    if (!mel_slotmap_alive(&g.devices, g.default_h))
        return MEL_AUDIOOUT_NULL;
    return (Mel_AudioOut){ g.default_h };
}

Mel_AudioOut mel_audioout_find(str8 stable_id)
{
    if (!g.initialized)
        return MEL_AUDIOOUT_NULL;
    for (usize i = 0; i < g.registry.count; i++)
        if (str8_equals(g.registry.items[i].stable_id, stable_id))
            return (Mel_AudioOut){ g.registry.items[i].handle };
    return MEL_AUDIOOUT_NULL;
}

Mel_AudioOut_Describe_Result mel_audioout_describe(Mel_AudioOut d, const Mel_Alloc* a)
{
    Mel_AudioOut_Describe_Result r = { 0 };
    Device_Slot*                 s = g.initialized ? device_slot(d.h) : NULL;
    if (!s)
    {
        mel_log_error("audioout", "describe on dead handle {index=%u, gen=%u}", d.h.index, d.h.generation);
        r.status = MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_NO_DEVICE;
        return r;
    }
    if (!a)
    {
        mel_log_error("audioout", "describe requires an allocator; got NULL");
        r.status = MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
        return r;
    }
    r.value.name = str8_dup(s->name, a);
    r.value.stable_id = str8_dup(s->stable_id, a);
    r.value.kind = s->kind;
    r.value.channels = s->channels;
    r.value.samplerate = s->samplerate;
    r.value.caps = s->caps;
    r.value.alloc = a;
    mel_array_init(&r.value.samplerates, a);
    for (usize i = 0; i < s->rates.count; i++)
        mel_array_push(&r.value.samplerates, s->rates.items[i]);
    r.status = MEL_AUDIOOUT_OK;
    return r;
}

void mel_audioout_describe_free(Mel_AudioOut_Describe_Result* r)
{
    if (!r || !r->value.alloc)
        return;
    if (r->value.name.data)
        mel_dealloc(r->value.alloc, r->value.name.data);
    if (r->value.stable_id.data)
        mel_dealloc(r->value.alloc, r->value.stable_id.data);
    mel_array_free(&r->value.samplerates);
    r->value.name = STR8_EMPTY;
    r->value.stable_id = STR8_EMPTY;
}

bool mel_audioout_alive(Mel_AudioOut d) { return g.initialized && mel_slotmap_alive(&g.devices, d.h); }

bool mel_audioout_equal(Mel_AudioOut a, Mel_AudioOut b) { return a.h.index == b.h.index && a.h.generation == b.h.generation; }

f32 mel_audioout_volume(Mel_AudioOut d)
{
    Device_Slot* s = g.initialized ? device_slot(d.h) : NULL;
    if (!s)
    {
        mel_log_error("audioout", "volume on dead handle");
        return 0.0f;
    }
    if (!s->caps.volume)
    {
        mel_log_error("audioout", "volume on device without volume capability: %.*s", (int)s->name.len, s->name.data);
        return 0.0f;
    }
    Provider_Entry* pe = provider_get(s->provider_idx);
    return (pe && pe->desc.volume) ? pe->desc.volume(pe->desc.user, s->stable_id) : 0.0f;
}

Mel_AudioOut_Status mel_audioout_set_volume(Mel_AudioOut d, f32 volume)
{
    assert(volume >= 0.0f && volume <= 1.0f);
    Device_Slot* s = g.initialized ? device_slot(d.h) : NULL;
    if (!s)
    {
        mel_log_error("audioout", "set_volume on dead handle");
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_LOST;
    }
    Provider_Entry* pe = provider_get(s->provider_idx);
    if (!s->caps.volume || !pe || !pe->desc.set_volume)
    {
        mel_log_error("audioout", "set_volume unsupported on %.*s", (int)s->name.len, s->name.data);
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    }
    return pe->desc.set_volume(pe->desc.user, s->stable_id, volume);
}

bool mel_audioout_muted(Mel_AudioOut d)
{
    Device_Slot* s = g.initialized ? device_slot(d.h) : NULL;
    if (!s)
    {
        mel_log_error("audioout", "muted on dead handle");
        return false;
    }
    if (!s->caps.mute)
    {
        mel_log_error("audioout", "muted on device without mute capability: %.*s", (int)s->name.len, s->name.data);
        return false;
    }
    Provider_Entry* pe = provider_get(s->provider_idx);
    return (pe && pe->desc.muted) ? pe->desc.muted(pe->desc.user, s->stable_id) : false;
}

Mel_AudioOut_Status mel_audioout_set_muted(Mel_AudioOut d, bool muted)
{
    Device_Slot* s = g.initialized ? device_slot(d.h) : NULL;
    if (!s)
    {
        mel_log_error("audioout", "set_muted on dead handle");
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_LOST;
    }
    Provider_Entry* pe = provider_get(s->provider_idx);
    if (!s->caps.mute || !pe || !pe->desc.set_muted)
    {
        mel_log_error("audioout", "set_muted unsupported on %.*s", (int)s->name.len, s->name.data);
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    }
    return pe->desc.set_muted(pe->desc.user, s->stable_id, muted);
}

Mel_AudioOut_Hotplug_Sub mel_audioout_subscribe(Mel_Executor* exec, Mel_AudioOut_Event_Callback cb, void* user)
{
    if (!g.initialized || g.hotplug == NULL)
    {
        mel_log_error("audioout", "subscribe before init; no channel");
        return MEL_AUDIOOUT_HOTPLUG_SUB_NULL;
    }
    Mel_Executor* target = exec != NULL ? exec : g.exec;
    Mel_Event_Sub sub = mel_event_subscribe_push(g.hotplug, target, (Mel_Event_Callback)cb, user);
    return (Mel_AudioOut_Hotplug_Sub){ sub.handle };
}

void mel_audioout_unsubscribe(Mel_AudioOut_Hotplug_Sub sub)
{
    if (!g.initialized || g.hotplug == NULL)
        return;
    mel_event_unsubscribe(g.hotplug, (Mel_Event_Sub){ sub.handle });
}

void* mel_audioout_native(Mel_AudioOut d)
{
    if (!g.initialized)
        return NULL;
    Device_Slot* s = device_slot(d.h);
    if (!s)
        return NULL;
    Provider_Entry* pe = provider_get(s->provider_idx);
    return (pe && pe->desc.native) ? pe->desc.native(pe->desc.user, s->stable_id) : NULL;
}

Mel_AudioOut_Status mel_audioout__open(Mel_AudioOut d, Mel_AudioOut_Format req, Mel_AudioOut_Format* granted, Mel_AudioOut_Source src)
{
    assert(granted != NULL);
    assert(src.pull != NULL);
    Device_Slot* s = g.initialized ? device_slot(d.h) : NULL;
    if (!s)
    {
        mel_log_error("audioout", "open on dead handle {index=%u, gen=%u}", d.h.index, d.h.generation);
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_LOST;
    }
    Provider_Entry* pe = provider_get(s->provider_idx);
    if (!pe || !pe->desc.open)
    {
        mel_log_error("audioout", "device %.*s has no open path", (int)s->name.len, s->name.data);
        return MEL_AUDIOOUT_ERROR | MEL_AUDIOOUT_RESULT_UNSUPPORTED;
    }
    return pe->desc.open(pe->desc.user, s->stable_id, req, granted, src);
}

static Provider_Entry* slot_provider(Mel_AudioOut d, Device_Slot** out_slot)
{
    Device_Slot* s = g.initialized ? device_slot(d.h) : NULL;
    *out_slot = s;
    return s ? provider_get(s->provider_idx) : NULL;
}

void mel_audioout__start(Mel_AudioOut d, void* token)
{
    Device_Slot*    s;
    Provider_Entry* pe = slot_provider(d, &s);
    if (pe && pe->desc.start)
        pe->desc.start(pe->desc.user, s->stable_id, token);
}

void mel_audioout__stop(Mel_AudioOut d, void* token)
{
    Device_Slot*    s;
    Provider_Entry* pe = slot_provider(d, &s);
    if (pe && pe->desc.stop)
        pe->desc.stop(pe->desc.user, s->stable_id, token);
}

void mel_audioout__close(Mel_AudioOut d, void* token)
{
    Device_Slot*    s;
    Provider_Entry* pe = slot_provider(d, &s);
    if (pe && pe->desc.close)
        pe->desc.close(pe->desc.user, s->stable_id, token);
}
