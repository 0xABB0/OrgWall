#include <display/display.h>
#include <display/events.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.slotmap/slotmap.h>
#include <event/event.h>
#include <log/log.h>

#include <string.h>

#include "display_backend.h"
#include "events_internal.h"

#define MEL_DISPLAY_REGISTRY_CAP 32
#define MEL_DISPLAY_EVENTS_CAP   128

typedef struct
{
    u64                    stable_id;
    Mel_Display_Descriptor desc;
} Display_Slot;

typedef struct
{
    u64                stable_id;
    Mel_SlotMap_Handle handle;
} Registry_Entry;

typedef struct
{
    bool                     initialized;
    const Mel_Alloc*         alloc;
    Mel_Executor*            exec;
    Mel_Display_Enumerate_Fn enumerate;
    Mel_SlotMap              slots;

    Mel_Event*    events;
    Mel_Event_Sub poll_sub;

    Registry_Entry entries[MEL_DISPLAY_REGISTRY_CAP];
    u32            entry_count;
} Registry;

static Registry g_reg;

static Mel_Display_Enumerate_Fn g_enumerate_override;

void mel_display__set_enumerate(Mel_Display_Enumerate_Fn fn)
{
    g_enumerate_override = fn;
    if (g_reg.initialized)
        g_reg.enumerate = fn != NULL ? fn : mel_display__enumerate;
}

static void events_overflow_report(const Mel_Event_Overflow_Info* info, void* user)
{
    MEL_UNUSED(user);
    mel_log_warn("display", "event channel full (capacity %u); dropping oldest, total_lagged=%llu", info->ring_capacity, (unsigned long long)info->total_lagged);
}

static void fire_event(Mel_Display_Event ev)
{
    if (g_reg.events != NULL)
        mel_event_fire(g_reg.events, &ev);
}

static void icc_free(const Mel_Alloc* a, Mel_Color_Icc_Profile* icc)
{
    if (icc->data)
        mel_dealloc(a, (void*)icc->data);
    icc->data = NULL;
    icc->size = 0;
}

static Registry_Entry* entry_by_stable_id(u64 id)
{
    for (u32 i = 0; i < g_reg.entry_count; i++)
        if (g_reg.entries[i].stable_id == id)
            return &g_reg.entries[i];
    return NULL;
}

void mel_display_init_ex(const Mel_Alloc* alloc, Mel_Executor* exec)
{
    if (g_reg.initialized)
        return;
    g_reg.alloc = alloc ? alloc : mel_alloc_heap();
    g_reg.exec = exec;
    g_reg.enumerate = g_enumerate_override != NULL ? g_enumerate_override : mel_display__enumerate;
    mel_slotmap_init(&g_reg.slots, g_reg.alloc, .item_size = sizeof(Display_Slot), .initial_capacity = 8);
    g_reg.entry_count = 0;

    g_reg.events = mel_event_create(g_reg.alloc, sizeof(Mel_Display_Event), MEL_DISPLAY_EVENTS_CAP, mel_event_policy_latest(events_overflow_report, NULL));
    g_reg.poll_sub = g_reg.events != NULL ? mel_event_subscribe_pull(g_reg.events, NULL) : MEL_EVENT_SUB_NULL;

    g_reg.initialized = true;
    mel_display_refresh();
}

void mel_display_init(const Mel_Alloc* alloc) { mel_display_init_ex(alloc, NULL); }

void mel_display_shutdown(void)
{
    if (!g_reg.initialized)
        return;
    if (g_reg.events != NULL)
        mel_event_unsubscribe(g_reg.events, g_reg.poll_sub);
    mel_event_destroy(g_reg.events);
    for (u32 i = 0; i < g_reg.entry_count; i++)
    {
        Display_Slot* s = mel_slotmap_get(&g_reg.slots, g_reg.entries[i].handle);
        if (s)
            icc_free(g_reg.alloc, &s->desc.icc_profile);
    }
    mel_slotmap_free(&g_reg.slots);
    memset(&g_reg, 0, sizeof g_reg);
}

u32 mel_display_refresh(void)
{
    if (!g_reg.initialized)
        mel_display_init(NULL);

    Mel_Display_Raw raw[MEL_DISPLAY_REGISTRY_CAP];
    u32             n = g_reg.enumerate(g_reg.alloc, raw, MEL_DISPLAY_REGISTRY_CAP);

    bool seen[MEL_DISPLAY_REGISTRY_CAP] = { 0 };

    for (u32 i = 0; i < n; i++)
    {
        Registry_Entry* e = entry_by_stable_id(raw[i].stable_id);
        if (e)
        {
            seen[(u32)(e - g_reg.entries)] = true;
            Display_Slot* s = mel_slotmap_get(&g_reg.slots, e->handle);
            if (!s)
            {
                icc_free(g_reg.alloc, &raw[i].desc.icc_profile);
                continue;
            }

            u32 fields = mel_display_events__changed_fields(&s->desc, &raw[i].desc);
            if (fields == 0)
            {
                icc_free(g_reg.alloc, &raw[i].desc.icc_profile);
                continue;
            }
            if (fields & MEL_DISPLAY_FIELD_ICC)
                icc_free(g_reg.alloc, &s->desc.icc_profile);
            else
                icc_free(g_reg.alloc, &raw[i].desc.icc_profile);

            if (fields & MEL_DISPLAY_FIELD_ICC)
            {
                s->desc = raw[i].desc;
            }
            else
            {
                Mel_Color_Icc_Profile keep = s->desc.icc_profile;
                s->desc = raw[i].desc;
                s->desc.icc_profile = keep;
            }

            Mel_Display_Event_Kind kind = (fields == MEL_DISPLAY_FIELD_STATE) ? MEL_DISPLAY_EVENT_POWER_STATE_CHANGED : MEL_DISPLAY_EVENT_CONFIGURATION_CHANGED;
            fire_event((Mel_Display_Event){ .kind = kind, .display = { e->handle }, .changed_fields = fields });
            continue;
        }

        if (g_reg.entry_count == MEL_DISPLAY_REGISTRY_CAP)
        {
            mel_log_warn("display", "registry full (%u); display id=%llu not tracked", MEL_DISPLAY_REGISTRY_CAP, (unsigned long long)raw[i].stable_id);
            icc_free(g_reg.alloc, &raw[i].desc.icc_profile);
            continue;
        }
        Display_Slot       slot = { .stable_id = raw[i].stable_id, .desc = raw[i].desc };
        Mel_SlotMap_Handle h = mel_slotmap_insert(&g_reg.slots, &slot);
        u32                idx = g_reg.entry_count++;
        g_reg.entries[idx] = (Registry_Entry){ .stable_id = raw[i].stable_id, .handle = h };
        seen[idx] = true;
        fire_event((Mel_Display_Event){ .kind = MEL_DISPLAY_EVENT_ADDED, .display = { h } });
    }

    for (u32 i = 0; i < g_reg.entry_count;)
    {
        if (seen[i])
        {
            i++;
            continue;
        }
        Mel_SlotMap_Handle h = g_reg.entries[i].handle;
        Display_Slot*      s = mel_slotmap_get(&g_reg.slots, h);
        if (s)
            icc_free(g_reg.alloc, &s->desc.icc_profile);
        mel_slotmap_remove(&g_reg.slots, h);
        fire_event((Mel_Display_Event){ .kind = MEL_DISPLAY_EVENT_REMOVED, .display = { h } });

        u32 last = g_reg.entry_count - 1;
        g_reg.entries[i] = g_reg.entries[last];
        seen[i] = seen[last];
        g_reg.entry_count--;
    }

    return mel_slotmap_count(&g_reg.slots);
}

u32 mel_display_count(void)
{
    if (!g_reg.initialized)
        return 0;
    return mel_slotmap_count(&g_reg.slots);
}

u32 mel_display_list(Mel_Display* out, u32 cap)
{
    if (!g_reg.initialized)
        return 0;
    u32 n = g_reg.entry_count < cap ? g_reg.entry_count : cap;
    for (u32 i = 0; i < n; i++)
        out[i] = (Mel_Display){ g_reg.entries[i].handle };
    return n;
}

Mel_Display_Describe_Result mel_display_describe(Mel_Display d)
{
    Mel_Display_Describe_Result r = { 0 };
    if (!g_reg.initialized || !mel_slotmap_alive(&g_reg.slots, d.h))
    {
        mel_log_error("display", "describe on dead handle {index=%u, gen=%u}", d.h.index, d.h.generation);
        r.status = MEL_DISPLAY_STATUS_INVALID_HANDLE;
        return r;
    }
    Display_Slot* s = mel_slotmap_get(&g_reg.slots, d.h);
    r.value = s->desc;
    r.status = MEL_DISPLAY_STATUS_OK;
    return r;
}

bool mel_display_alive(Mel_Display d) { return g_reg.initialized && mel_slotmap_alive(&g_reg.slots, d.h); }

bool mel_display_equal(Mel_Display a, Mel_Display b) { return a.h.index == b.h.index && a.h.generation == b.h.generation; }

bool mel_display__stable_id(Mel_Display d, u64* out_id)
{
    if (!mel_display_alive(d))
        return false;
    Display_Slot* s = mel_slotmap_get(&g_reg.slots, d.h);
    *out_id = s->stable_id;
    return true;
}

const Mel_Display_Descriptor* mel_display__descriptor(Mel_Display d)
{
    if (!mel_display_alive(d))
        return NULL;
    Display_Slot* s = mel_slotmap_get(&g_reg.slots, d.h);
    return &s->desc;
}

u32 mel_display_poll_events(Mel_Display_Event* out, u32 cap)
{
    if (!g_reg.initialized || g_reg.events == NULL)
        return 0;
    u32 n = 0;
    for (; n < cap && mel_event_pull(g_reg.events, g_reg.poll_sub, &out[n]); n++)
        ;
    return n;
}

Mel_Display_Subscription mel_display_subscribe(Mel_Executor* exec, Mel_Display_Event_Callback cb, void* user)
{
    if (!g_reg.initialized || g_reg.events == NULL)
    {
        mel_log_error("display", "subscribe before init; no channel");
        return MEL_DISPLAY_SUBSCRIPTION_NULL;
    }
    Mel_Executor* target = exec != NULL ? exec : g_reg.exec;
    if (target == NULL)
    {
        mel_log_error("display", "subscribe needs an executor; none passed and registry has none (pull-only init)");
        return MEL_DISPLAY_SUBSCRIPTION_NULL;
    }
    Mel_Event_Sub sub = mel_event_subscribe_push(g_reg.events, target, (Mel_Event_Callback)cb, user);
    return (Mel_Display_Subscription){ sub.handle };
}

void mel_display_unsubscribe(Mel_Display_Subscription sub)
{
    if (!g_reg.initialized || g_reg.events == NULL)
        return;
    mel_event_unsubscribe(g_reg.events, (Mel_Event_Sub){ sub.handle });
}
