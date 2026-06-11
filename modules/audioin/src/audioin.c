#include "audioin_internal.h"

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/slotmap.h>
#include <collection/array.h>
#include <collection/list.h>
#include <future/future.h>
#include <executor/executor.h>
#include <event/event.h>
#include <string/str8.h>
#include <log/log.h>

#include <stdatomic.h>
#include <string.h>

#define MEL_AUDIOIN_HOTPLUG_RING_CAP 64u

typedef struct
{
    Mel_AudioIn_Provider_Desc desc;
    u32                       generation;
    bool                      active;
} Provider_Entry;

typedef struct
{
    u32                     provider_idx;
    str8                    stable_id;
    str8                    name;
    const mel_audioin_kind* kind;
    u32                     channels;
    u32                     samplerate;
    Mel_AudioIn_Rates       rates;
    Mel_AudioIn_Caps        caps;
} Device_Slot;

typedef struct
{
    str8               stable_id;
    u32                provider_idx;
    Mel_SlotMap_Handle handle;
} Reg_Entry;

typedef struct
{
    Mel_Future              future;
    const Mel_Alloc*        alloc;
    const mel_audioin_auth* auth;
    _Atomic(u32)            pending;
    bool                    resolved;
} Auth_Job;

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

    Auth_Job* pending_auth;
} AudioIn;

static AudioIn g;

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
    mel_log_warn("audioin", "hotplug channel full (capacity %u); total_lagged=%llu", info->ring_capacity, (unsigned long long)info->total_lagged);
}

static void fire_hotplug(Mel_AudioIn_Event ev)
{
    if (g.hotplug != NULL)
        mel_event_fire(g.hotplug, &ev);
}

Mel_AudioIn_Provider mel_audioin_provider_register(const Mel_AudioIn_Provider_Desc* desc)
{
    Provider_Entry e = { .desc = *desc, .generation = ++g.provider_gen, .active = true };
    u32            idx = (u32)g.providers.count;
    mel_array_push(&g.providers, e);
    return (Mel_AudioIn_Provider){ .index = idx, .generation = e.generation };
}

void mel_audioin_provider_unregister(Mel_AudioIn_Provider p)
{
    if (p.index < g.providers.count && g.providers.items[p.index].generation == p.generation)
        g.providers.items[p.index].active = false;
}

static void notify_job(void* data)
{
    MEL_UNUSED(data);
    if (g.initialized)
        mel_audioin_refresh();
}

void mel_audioin_provider_notify(Mel_AudioIn_Provider p)
{
    if (!g.initialized)
        return;
    if (p.index < g.providers.count && g.providers.items[p.index].generation == p.generation)
        mel_executor_call(g.exec, notify_job, NULL, g.alloc);
}

void mel_audioin_init(const Mel_Alloc* alloc, Mel_Executor* deliver)
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
    g.pending_auth = NULL;
    g.hotplug = mel_event_create(g.alloc, sizeof(Mel_AudioIn_Event), MEL_AUDIOIN_HOTPLUG_RING_CAP, mel_event_policy_latest(hotplug_overflow, NULL));
    g.initialized = true;
    mel_audioin__register_host_providers();
    mel_audioin__publish_register_provider(g.alloc);
    mel_audioin_refresh();
}

static void device_teardown(Device_Slot* s)
{
    str8_owned_free(&s->stable_id);
    str8_owned_free(&s->name);
    mel_array_free(&s->rates);
}

void mel_audioin_shutdown(void)
{
    assert(g.initialized);
    if (!g.initialized)
        return;
    if (g.pending_auth && !g.pending_auth->resolved)
    {
        g.pending_auth->resolved = true;
        mel_future_cancel(&g.pending_auth->future);
    }
    g.pending_auth = NULL;

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

static bool rates_equal(const Mel_AudioIn_Rates* a, const u32* b, u32 b_count)
{
    if (a->count != b_count)
        return false;
    for (u32 i = 0; i < b_count; i++)
        if (a->items[i] != b[i])
            return false;
    return true;
}

static void rates_replace(Mel_AudioIn_Rates* dst, const u32* src, u32 count)
{
    mel_array_clear(dst);
    for (u32 i = 0; i < count; i++)
        mel_array_push(dst, src[i]);
}

static bool device_update(Device_Slot* s, const Mel_AudioIn_Raw* raw)
{
    const mel_audioin_kind* kind = raw->kind ? raw->kind : &mel_audioin_unknown;
    bool                    changed = false;

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
    if (s->caps.gain != raw->caps.gain)
    {
        s->caps = raw->caps;
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

static bool refresh_accept(const Mel_AudioIn_Raw* raw, void* user)
{
    Refresh_Ctx* ctx = user;
    u32          pi = ctx->pi;

    Reg_Entry* e = reg_find(pi, raw->stable_id);
    if (e)
    {
        ctx->seen->items[(usize)(e - g.registry.items)] = true;
        Device_Slot* s = device_slot(e->handle);
        if (s && device_update(s, raw))
            fire_hotplug((Mel_AudioIn_Event){ .device = { e->handle }, .kind = s->kind, .changed = true });
        return true;
    }

    Device_Slot slot;
    memset(&slot, 0, sizeof slot);
    slot.provider_idx = pi;
    slot.stable_id = str8_dup(raw->stable_id, g.alloc);
    slot.name = str8_dup(raw->name, g.alloc);
    slot.kind = raw->kind ? raw->kind : &mel_audioin_unknown;
    slot.channels = raw->channels;
    slot.samplerate = raw->samplerate;
    slot.caps = raw->caps;
    mel_array_init(&slot.rates, g.alloc);
    rates_replace(&slot.rates, raw->samplerates, raw->samplerate_count);

    Mel_SlotMap_Handle h = mel_slotmap_insert(&g.devices, &slot);
    Device_Slot*       inserted = device_slot(h);
    Reg_Entry          re = { .stable_id = inserted->stable_id, .provider_idx = pi, .handle = h };
    mel_array_push(&g.registry, re);
    mel_array_push(ctx->seen, true);
    mel_log_info("audioin", "device added: %.*s [%s] id=%.*s", (int)inserted->name.len, inserted->name.data, mel_audioin_kind_name(inserted->kind), (int)inserted->stable_id.len, inserted->stable_id.data);
    fire_hotplug((Mel_AudioIn_Event){ .device = { h }, .kind = inserted->kind, .added = true });
    return true;
}

u32 mel_audioin_refresh(void)
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
        Mel_SlotMap_Handle      h = g.registry.items[i].handle;
        Device_Slot*            s = device_slot(h);
        const mel_audioin_kind* kind = s ? s->kind : &mel_audioin_unknown;
        if (s)
        {
            mel_log_info("audioin", "device removed: %.*s", (int)s->name.len, s->name.data);
            device_teardown(s);
        }
        mel_slotmap_remove(&g.devices, h);
        fire_hotplug((Mel_AudioIn_Event){ .device = { h }, .kind = kind, .removed = true });

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
        fire_hotplug((Mel_AudioIn_Event){ .device = { def }, .kind = s ? s->kind : &mel_audioin_unknown, .default_changed = true });
    }

    if (g.registry.count == 0)
        mel_log_warn("audioin", "zero input devices enumerated across %u provider(s)", (u32)g.providers.count);

    return (u32)g.registry.count;
}

u32 mel_audioin_count(void) { return g.initialized ? (u32)g.registry.count : 0; }

u32 mel_audioin_list(Mel_AudioIn* out, u32 cap)
{
    if (!g.initialized)
        return 0;
    u32 n = g.registry.count < cap ? (u32)g.registry.count : cap;
    for (u32 i = 0; i < n; i++)
        out[i] = (Mel_AudioIn){ g.registry.items[i].handle };
    return n;
}

Mel_AudioIn mel_audioin_default(void)
{
    if (!g.initialized)
        return MEL_AUDIOIN_NULL;
    if (!mel_slotmap_alive(&g.devices, g.default_h))
        return MEL_AUDIOIN_NULL;
    return (Mel_AudioIn){ g.default_h };
}

Mel_AudioIn mel_audioin_find(str8 stable_id)
{
    if (!g.initialized)
        return MEL_AUDIOIN_NULL;
    for (usize i = 0; i < g.registry.count; i++)
        if (str8_equals(g.registry.items[i].stable_id, stable_id))
            return (Mel_AudioIn){ g.registry.items[i].handle };
    return MEL_AUDIOIN_NULL;
}

Mel_AudioIn_Describe_Result mel_audioin_describe(Mel_AudioIn d, const Mel_Alloc* a)
{
    Mel_AudioIn_Describe_Result r = { 0 };
    Device_Slot*                s = g.initialized ? device_slot(d.h) : NULL;
    if (!s)
    {
        mel_log_error("audioin", "describe on dead handle {index=%u, gen=%u}", d.h.index, d.h.generation);
        r.status = MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_NO_DEVICE;
        return r;
    }
    if (!a)
    {
        mel_log_error("audioin", "describe requires an allocator; got NULL");
        r.status = MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
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
    r.status = MEL_AUDIOIN_OK;
    return r;
}

void mel_audioin_describe_free(Mel_AudioIn_Describe_Result* r)
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

bool mel_audioin_alive(Mel_AudioIn d) { return g.initialized && mel_slotmap_alive(&g.devices, d.h); }

bool mel_audioin_equal(Mel_AudioIn a, Mel_AudioIn b) { return a.h.index == b.h.index && a.h.generation == b.h.generation; }

f32 mel_audioin_gain(Mel_AudioIn d)
{
    Device_Slot* s = g.initialized ? device_slot(d.h) : NULL;
    if (!s)
    {
        mel_log_error("audioin", "gain on dead handle");
        return 0.0f;
    }
    if (!s->caps.gain)
    {
        mel_log_error("audioin", "gain on device without gain capability: %.*s", (int)s->name.len, s->name.data);
        return 0.0f;
    }
    Provider_Entry* pe = provider_get(s->provider_idx);
    return (pe && pe->desc.gain) ? pe->desc.gain(pe->desc.user, s->stable_id) : 0.0f;
}

Mel_AudioIn_Status mel_audioin_set_gain(Mel_AudioIn d, f32 gain)
{
    assert(gain >= 0.0f && gain <= 1.0f);
    Device_Slot* s = g.initialized ? device_slot(d.h) : NULL;
    if (!s)
    {
        mel_log_error("audioin", "set_gain on dead handle");
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_LOST;
    }
    Provider_Entry* pe = provider_get(s->provider_idx);
    if (!s->caps.gain || !pe || !pe->desc.set_gain)
    {
        mel_log_error("audioin", "set_gain unsupported on %.*s", (int)s->name.len, s->name.data);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }
    return pe->desc.set_gain(pe->desc.user, s->stable_id, gain);
}

Mel_AudioIn_Hotplug_Sub mel_audioin_subscribe(Mel_Executor* exec, Mel_AudioIn_Event_Callback cb, void* user)
{
    if (!g.initialized || g.hotplug == NULL)
    {
        mel_log_error("audioin", "subscribe before init; no channel");
        return MEL_AUDIOIN_HOTPLUG_SUB_NULL;
    }
    Mel_Executor* target = exec != NULL ? exec : g.exec;
    Mel_Event_Sub sub = mel_event_subscribe_push(g.hotplug, target, (Mel_Event_Callback)cb, user);
    return (Mel_AudioIn_Hotplug_Sub){ sub.handle };
}

void mel_audioin_unsubscribe(Mel_AudioIn_Hotplug_Sub sub)
{
    if (!g.initialized || g.hotplug == NULL)
        return;
    mel_event_unsubscribe(g.hotplug, (Mel_Event_Sub){ sub.handle });
}

static bool provider_has_devices(u32 idx)
{
    for (usize i = 0; i < g.registry.count; i++)
        if (g.registry.items[i].provider_idx == idx)
            return true;
    return false;
}

static const mel_audioin_auth* provider_authorization(const Provider_Entry* pe) { return pe->desc.authorization ? pe->desc.authorization(pe->desc.user) : &mel_audioin_auth_granted; }

static const mel_audioin_auth* compute_authorization(void)
{
    const mel_audioin_auth* worst = NULL;
    for (u32 pi = 0; pi < g.providers.count; pi++)
    {
        Provider_Entry* pe = &g.providers.items[pi];
        if (!pe->active || !provider_has_devices(pi))
            continue;
        const mel_audioin_auth* a = provider_authorization(pe);
        if (!worst || a->restrictiveness > worst->restrictiveness)
            worst = a;
    }
    if (worst)
        return worst;
    for (u32 pi = 0; pi < g.providers.count; pi++)
    {
        Provider_Entry* pe = &g.providers.items[pi];
        if (!pe->active || !pe->desc.authorization)
            continue;
        const mel_audioin_auth* a = provider_authorization(pe);
        if (!worst || a->restrictiveness > worst->restrictiveness)
            worst = a;
    }
    return worst ? worst : &mel_audioin_auth_not_determined;
}

const mel_audioin_auth* mel_audioin_authorization(void)
{
    if (!g.initialized)
        return &mel_audioin_auth_not_determined;
    return compute_authorization();
}

static void auth_resolve(Auth_Job* j, const mel_audioin_auth* auth)
{
    if (!j || j->resolved)
        return;
    j->resolved = true;
    j->auth = auth;
    if (g.pending_auth == j)
        g.pending_auth = NULL;
    mel_future_resolve(&j->future, (void*)auth, mel_audioin_auth_is_granted(auth) ? MEL_FUTURE_OK : MEL_FUTURE_ERROR);
}

static void core_on_auth(void* token, const mel_audioin_auth* auth)
{
    MEL_UNUSED(auth);
    Auth_Job* j = token;
    if (!j)
        return;
    if (atomic_fetch_sub_explicit(&j->pending, 1u, memory_order_acq_rel) == 1u)
        auth_resolve(j, compute_authorization());
}

Mel_Future* mel_audioin_authorize(const Mel_Alloc* a)
{
    if (!g.initialized)
        return NULL;
    const Mel_Alloc* alloc = a ? a : g.alloc;
    Auth_Job*        j = mel_alloc_type(alloc, Auth_Job);
    if (!j)
        return NULL;
    memset(j, 0, sizeof *j);
    j->alloc = alloc;
    mel_future_init(&j->future, NULL, alloc);
    g.pending_auth = j;

    u32 prompters = 0;
    for (u32 pi = 0; pi < g.providers.count; pi++)
        if (g.providers.items[pi].active && g.providers.items[pi].desc.authorize)
            prompters++;

    if (prompters == 0)
    {
        auth_resolve(j, compute_authorization());
        return &j->future;
    }

    atomic_store_explicit(&j->pending, prompters, memory_order_release);
    Mel_AudioIn_Sink sink = { .on_auth = core_on_auth, .token = j };
    for (u32 pi = 0; pi < g.providers.count; pi++)
    {
        Provider_Entry* pe = &g.providers.items[pi];
        if (pe->active && pe->desc.authorize)
            pe->desc.authorize(pe->desc.user, sink);
    }
    return &j->future;
}

const mel_audioin_auth* mel_audioin_future_auth(const Mel_Future* f)
{
    const mel_audioin_auth* a = f ? (const mel_audioin_auth*)mel_future_value((Mel_Future*)f) : NULL;
    return a ? a : &mel_audioin_auth_not_determined;
}

void mel_audioin_future_free(Mel_Future* f)
{
    if (!f)
        return;
    Auth_Job* j = mel_container_of(f, Auth_Job, future);
    if (g.pending_auth == j)
        g.pending_auth = NULL;
    mel_dealloc(j->alloc, j);
}

void* mel_audioin_native(Mel_AudioIn d)
{
    if (!g.initialized)
        return NULL;
    Device_Slot* s = device_slot(d.h);
    if (!s)
        return NULL;
    Provider_Entry* pe = provider_get(s->provider_idx);
    return (pe && pe->desc.native) ? pe->desc.native(pe->desc.user, s->stable_id) : NULL;
}

Mel_AudioIn_Status mel_audioin__open(Mel_AudioIn d, Mel_AudioIn_Sink sink)
{
    Device_Slot* s = g.initialized ? device_slot(d.h) : NULL;
    if (!s)
    {
        mel_log_error("audioin", "open on dead handle {index=%u, gen=%u}", d.h.index, d.h.generation);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_LOST;
    }
    Provider_Entry* pe = provider_get(s->provider_idx);
    if (!pe || !pe->desc.open)
    {
        mel_log_error("audioin", "device %.*s has no open path", (int)s->name.len, s->name.data);
        return MEL_AUDIOIN_ERROR | MEL_AUDIOIN_RESULT_UNSUPPORTED;
    }
    return pe->desc.open(pe->desc.user, s->stable_id, sink);
}

void mel_audioin__close(Mel_AudioIn d, void* token)
{
    Device_Slot* s = g.initialized ? device_slot(d.h) : NULL;
    if (!s)
        return;
    Provider_Entry* pe = provider_get(s->provider_idx);
    if (pe && pe->desc.close)
        pe->desc.close(pe->desc.user, s->stable_id, token);
}
