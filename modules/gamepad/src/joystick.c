#include <gamepad/joystick.h>
#include <gamepad/events.h>
#include <gamepad/provider.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/slotmap.h>
#include <collection/array.h>
#include <event/event.h>
#include <log/log.h>
#include <thread/thread.h>
#include <debug/assert.h>

#include <string/str8.h>

#include <string.h>

#include "joystick_backend.h"

#define MEL_JOYSTICK_EVENTS_CAP 128

typedef struct
{
    Mel_Joystick_Provider_Desc desc;
    u32                        generation;
    bool                       active;
} Provider_Entry;

typedef struct
{
    u32                     provider_idx;
    u64                     stable_id;
    Mel_Joystick_Descriptor desc;
} Device_Slot;

typedef struct
{
    u32                provider_idx;
    u64                stable_id;
    Mel_SlotMap_Handle handle;
} Reg_Entry;

typedef struct
{
    Mel_Joystick_Raw raw;
    u32              prov;
} Gathered;

typedef struct
{
    bool             initialized;
    const Mel_Alloc* alloc;
    Mel_Executor*    exec;
    Mel_SlotMap      devices;
    Mel_Array(Provider_Entry) providers;
    Mel_Array(Reg_Entry) registry;
    Mel_Event*    events;
    Mel_Event_Sub poll_sub;
    u32           provider_gen;
    Mel_Thread_Id owner;
} Joystick;

static Joystick g;

static void assert_affinity(void)
{
    mel_assert(mel_thread_id_equal(g.owner, mel_thread_current_id()));
}

static Mel_Joystick_Host_Register_Fn g_host_register_override;

void mel_joystick__set_host_register(Mel_Joystick_Host_Register_Fn fn) { g_host_register_override = fn; }

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

static void events_overflow_report(const Mel_Event_Overflow_Info* info, void* user)
{
    MEL_UNUSED(user);
    mel_log_warn("gamepad", "event channel full (capacity %u); dropping oldest, total_lagged=%llu", info->ring_capacity, (unsigned long long)info->total_lagged);
}

static void fire_event(Mel_Joystick_Event ev)
{
    if (g.events != NULL)
        mel_event_fire(g.events, &ev);
}

static u32 changed_fields(const Mel_Joystick_Descriptor* a, const Mel_Joystick_Descriptor* b)
{
    u32 fields = 0;
    if (memcmp(&a->power, &b->power, sizeof a->power) != 0)
        fields |= MEL_JOYSTICK_FIELD_POWER;
    if (a->player_index != b->player_index)
        fields |= MEL_JOYSTICK_FIELD_PLAYER_INDEX;
    if (memcmp(&a->features, &b->features, sizeof a->features) != 0)
        fields |= MEL_JOYSTICK_FIELD_FEATURES;
    if (!str8_equals(a->name, b->name))
        fields |= MEL_JOYSTICK_FIELD_NAME;
    return fields;
}

static void slot_release_strings(Device_Slot* s)
{
    if (s->desc.name.data != NULL)
    {
        mel_dealloc(g.alloc, s->desc.name.data);
        s->desc.name = (str8){ 0 };
    }
    if (s->desc.serial.data != NULL)
    {
        mel_dealloc(g.alloc, s->desc.serial.data);
        s->desc.serial = (str8){ 0 };
    }
}

static void slot_adopt(Device_Slot* s, const Mel_Joystick_Descriptor* src)
{
    Mel_Joystick_Descriptor d = *src;
    d.name = src->name.len > 0 ? str8_dup_alloc(src->name, g.alloc) : (str8){ 0 };
    d.serial = src->serial.len > 0 ? str8_dup_alloc(src->serial, g.alloc) : (str8){ 0 };
    s->desc = d;
}

Mel_Joystick_Provider mel_joystick_provider_register(const Mel_Joystick_Provider_Desc* desc)
{
    if (g.initialized)
        assert_affinity();
    Provider_Entry e = { .desc = *desc, .generation = ++g.provider_gen, .active = true };
    u32            idx = (u32)g.providers.count;
    mel_array_push(&g.providers, e);
    return (Mel_Joystick_Provider){ .index = idx, .generation = e.generation };
}

void mel_joystick_provider_unregister(Mel_Joystick_Provider p)
{
    if (g.initialized)
        assert_affinity();
    if (p.index < g.providers.count && g.providers.items[p.index].generation == p.generation)
        g.providers.items[p.index].active = false;
}

void mel_joystick_init_ex(const Mel_Alloc* alloc, Mel_Executor* exec)
{
    if (g.initialized)
        return;
    g.alloc = alloc ? alloc : mel_alloc_heap();
    g.exec = exec;
    mel_slotmap_init(&g.devices, g.alloc, .item_size = sizeof(Device_Slot), .initial_capacity = 4);
    mel_array_init(&g.providers, g.alloc);
    mel_array_init(&g.registry, g.alloc);
    g.provider_gen = 0;
    g.events = mel_event_create(g.alloc, sizeof(Mel_Joystick_Event), MEL_JOYSTICK_EVENTS_CAP, mel_event_policy_latest(events_overflow_report, NULL));
    g.poll_sub = g.events != NULL ? mel_event_subscribe_pull(g.events, NULL) : MEL_EVENT_SUB_NULL;
    g.owner = mel_thread_current_id();
    g.initialized = true;
    if (g_host_register_override)
        g_host_register_override(g.alloc);
    else
        mel_joystick__register_host_providers(g.alloc);
    mel_joystick_refresh();
}

void mel_joystick_init(const Mel_Alloc* alloc) { mel_joystick_init_ex(alloc, NULL); }

void mel_joystick_shutdown(void)
{
    if (!g.initialized)
        return;
    assert_affinity();
    for (usize i = 0; i < g.registry.count; i++)
    {
        Device_Slot* s = device_slot(g.registry.items[i].handle);
        if (s)
            slot_release_strings(s);
        Provider_Entry* prov = provider_get(g.registry.items[i].provider_idx);
        if (prov && prov->desc.close)
            prov->desc.close(prov->desc.user, g.registry.items[i].stable_id);
    }
    if (g.events != NULL)
    {
        mel_event_unsubscribe(g.events, g.poll_sub);
        mel_event_destroy(g.events);
    }
    mel_array_free(&g.registry);
    mel_array_free(&g.providers);
    mel_slotmap_free(&g.devices);
    memset(&g, 0, sizeof g);
}

u32 mel_joystick_refresh(void)
{
    if (!g.initialized)
        mel_joystick_init(NULL);
    assert_affinity();

    Mel_Array(Gathered) gs;
    mel_array_init(&gs, g.alloc);

    for (u32 pi = 0; pi < g.providers.count; pi++)
    {
        Provider_Entry* pe = &g.providers.items[pi];
        if (!pe->active || !pe->desc.enumerate)
            continue;
        Mel_Array(Mel_Joystick_Raw) tmp;
        mel_array_init(&tmp, g.alloc);
        mel_array_reserve(&tmp, 8);
        u32 n = pe->desc.enumerate(pe->desc.user, tmp.items, (u32)tmp.capacity);
        while (n == tmp.capacity)
        {
            mel_array_reserve(&tmp, tmp.capacity * 2);
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
                u32 fields = changed_fields(&s->desc, &gt->raw.desc);
                slot_release_strings(s);
                slot_adopt(s, &gt->raw.desc);
                if (fields != 0)
                    fire_event((Mel_Joystick_Event){ .kind = MEL_JOYSTICK_EVENT_CHANGED, .joystick = { e->handle }, .changed_fields = fields });
            }
            continue;
        }
        Device_Slot slot = { .provider_idx = gt->prov, .stable_id = gt->raw.stable_id };
        slot_adopt(&slot, &gt->raw.desc);
        Mel_SlotMap_Handle h = mel_slotmap_insert(&g.devices, &slot);
        Reg_Entry          re = { .provider_idx = gt->prov, .stable_id = gt->raw.stable_id, .handle = h };
        mel_array_push(&g.registry, re);
        mel_array_push(&seen, true);
        fire_event((Mel_Joystick_Event){ .kind = MEL_JOYSTICK_EVENT_ADDED, .joystick = { h } });
        mel_log_info("gamepad", "joystick added: provider=%u stable_id=%llu", gt->prov, (unsigned long long)gt->raw.stable_id);
    }

    for (usize i = 0; i < g.registry.count;)
    {
        if (i < seen.count && seen.items[i])
        {
            i++;
            continue;
        }
        Mel_SlotMap_Handle h = g.registry.items[i].handle;
        Device_Slot*       rs = device_slot(h);
        if (rs)
            slot_release_strings(rs);
        Provider_Entry* prov = provider_get(g.registry.items[i].provider_idx);
        if (prov && prov->desc.close)
            prov->desc.close(prov->desc.user, g.registry.items[i].stable_id);
        mel_slotmap_remove(&g.devices, h);
        fire_event((Mel_Joystick_Event){ .kind = MEL_JOYSTICK_EVENT_REMOVED, .joystick = { h } });

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

u32 mel_joystick_count(void) { return g.initialized ? (u32)g.registry.count : 0; }

u32 mel_joystick_list(Mel_Joystick* out, u32 cap)
{
    if (!g.initialized)
        return 0;
    u32 n = g.registry.count < cap ? (u32)g.registry.count : cap;
    for (u32 i = 0; i < n; i++)
        out[i] = (Mel_Joystick){ g.registry.items[i].handle };
    return n;
}

Mel_Joystick_Describe_Result mel_joystick_describe(Mel_Joystick j)
{
    Mel_Joystick_Describe_Result r = { 0 };
    Device_Slot*                 s = g.initialized ? device_slot(j.h) : NULL;
    if (!s)
    {
        mel_log_error("gamepad", "describe on dead joystick {index=%u, gen=%u}", j.h.index, j.h.generation);
        r.status = MEL_JOYSTICK_ERROR | MEL_JOYSTICK_INVALID_HANDLE;
        return r;
    }
    r.value = s->desc;
    r.status = MEL_JOYSTICK_OK;
    return r;
}

bool mel_joystick_alive(Mel_Joystick j) { return g.initialized && mel_slotmap_alive(&g.devices, j.h); }

bool mel_joystick_equal(Mel_Joystick a, Mel_Joystick b) { return a.h.index == b.h.index && a.h.generation == b.h.generation; }

bool mel_joystick__lookup(Mel_Joystick j, u32* out_provider_idx, u64* out_stable_id)
{
    Device_Slot* s = g.initialized ? device_slot(j.h) : NULL;
    if (!s)
        return false;
    if (out_provider_idx)
        *out_provider_idx = s->provider_idx;
    if (out_stable_id)
        *out_stable_id = s->stable_id;
    return true;
}

const Mel_Joystick_Descriptor* mel_joystick__descriptor(Mel_Joystick j)
{
    Device_Slot* s = g.initialized ? device_slot(j.h) : NULL;
    return s ? &s->desc : NULL;
}

const Mel_Joystick_Provider_Desc* mel_joystick__provider_desc(u32 provider_idx)
{
    Provider_Entry* p = provider_get(provider_idx);
    return p ? &p->desc : NULL;
}

Mel_Joystick_State_Result mel_joystick_poll(Mel_Joystick j)
{
    Mel_Joystick_State_Result r = { 0 };
    Device_Slot*              s = g.initialized ? device_slot(j.h) : NULL;
    if (!s)
    {
        mel_log_error("gamepad", "poll on dead joystick {index=%u, gen=%u}", j.h.index, j.h.generation);
        r.status = MEL_JOYSTICK_ERROR | MEL_JOYSTICK_INVALID_HANDLE;
        return r;
    }
    Provider_Entry* prov = provider_get(s->provider_idx);
    if (!prov || !prov->desc.poll)
    {
        r.status = MEL_JOYSTICK_ERROR | MEL_JOYSTICK_NO_PROVIDER;
        return r;
    }
    if (!prov->desc.poll(prov->desc.user, s->stable_id, &r.value))
    {
        r.status = MEL_JOYSTICK_ERROR | MEL_JOYSTICK_DEVICE_LOST;
        return r;
    }
    r.status = MEL_JOYSTICK_OK;
    return r;
}

Mel_Joystick_Status mel_joystick_rumble(Mel_Joystick j, Mel_Joystick_Rumble rumble)
{
    Device_Slot* s = g.initialized ? device_slot(j.h) : NULL;
    if (!s)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_INVALID_HANDLE;
    Provider_Entry* prov = provider_get(s->provider_idx);
    if (!prov || !prov->desc.rumble)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_NO_PROVIDER;
    Mel_Joystick_Status status = prov->desc.rumble(prov->desc.user, s->stable_id, rumble);
    if ((rumble.left_trigger > 0.0f || rumble.right_trigger > 0.0f) && !s->desc.features.trigger_rumble)
        status |= MEL_JOYSTICK_TRIGGER_RUMBLE_OFF | MEL_JOYSTICK_WARNED;
    return status;
}

Mel_Joystick_Status mel_joystick_led(Mel_Joystick j, Mel_Joystick_Led led)
{
    Device_Slot* s = g.initialized ? device_slot(j.h) : NULL;
    if (!s)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_INVALID_HANDLE;
    if (!s->desc.features.rgb_led && !s->desc.features.player_led)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_LED_UNSUPPORTED;
    Provider_Entry* prov = provider_get(s->provider_idx);
    if (!prov || !prov->desc.led)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_NO_PROVIDER;
    return prov->desc.led(prov->desc.user, s->stable_id, led);
}

Mel_Joystick_Status mel_joystick_player_index(Mel_Joystick j, i32 player_index)
{
    Device_Slot* s = g.initialized ? device_slot(j.h) : NULL;
    if (!s)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_INVALID_HANDLE;
    Provider_Entry* prov = provider_get(s->provider_idx);
    if (!prov || !prov->desc.set_player_index)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_NO_PROVIDER;
    Mel_Joystick_Status status = prov->desc.set_player_index(prov->desc.user, s->stable_id, player_index);
    if (!mel_joystick_failed(status))
        s->desc.player_index = player_index;
    return status;
}

Mel_Joystick_Status mel_joystick_effect(Mel_Joystick j, const void* data, usize size)
{
    Device_Slot* s = g.initialized ? device_slot(j.h) : NULL;
    if (!s)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_INVALID_HANDLE;
    if (!s->desc.features.manufacturer_effects)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_EFFECT_REJECTED;
    Provider_Entry* prov = provider_get(s->provider_idx);
    if (!prov || !prov->desc.effect)
        return MEL_JOYSTICK_ERROR | MEL_JOYSTICK_NO_PROVIDER;
    return prov->desc.effect(prov->desc.user, s->stable_id, data, size);
}

void* mel_joystick_steam_input_handle(Mel_Joystick j)
{
    Device_Slot* s = g.initialized ? device_slot(j.h) : NULL;
    if (!s)
        return NULL;
    Provider_Entry* prov = provider_get(s->provider_idx);
    return (prov && prov->desc.steam_input_handle) ? prov->desc.steam_input_handle(prov->desc.user, s->stable_id) : NULL;
}

void* mel_joystick_native(Mel_Joystick j)
{
    Device_Slot* s = g.initialized ? device_slot(j.h) : NULL;
    if (!s)
        return NULL;
    Provider_Entry* prov = provider_get(s->provider_idx);
    return (prov && prov->desc.native) ? prov->desc.native(prov->desc.user, s->stable_id) : NULL;
}

u32 mel_joystick_poll_events(Mel_Joystick_Event* out, u32 cap)
{
    if (!g.initialized || g.events == NULL)
        return 0;
    u32 n = 0;
    for (; n < cap && mel_event_pull(g.events, g.poll_sub, &out[n]); n++)
        ;
    return n;
}

Mel_Joystick_Subscription mel_joystick_subscribe(Mel_Executor* exec, Mel_Joystick_Event_Callback cb, void* user)
{
    if (!g.initialized || g.events == NULL)
    {
        mel_log_error("gamepad", "subscribe before init; no channel");
        return MEL_JOYSTICK_SUBSCRIPTION_NULL;
    }
    Mel_Executor* target = exec != NULL ? exec : g.exec;
    if (target == NULL)
    {
        mel_log_error("gamepad", "subscribe needs an executor; none passed and registry has none (pull-only init)");
        return MEL_JOYSTICK_SUBSCRIPTION_NULL;
    }
    Mel_Event_Sub sub = mel_event_subscribe_push(g.events, target, (Mel_Event_Callback)cb, user);
    return (Mel_Joystick_Subscription){ sub.handle };
}

void mel_joystick_unsubscribe(Mel_Joystick_Subscription sub)
{
    if (!g.initialized || g.events == NULL)
        return;
    mel_event_unsubscribe(g.events, (Mel_Event_Sub){ sub.handle });
}
