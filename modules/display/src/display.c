#include <display/display.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.slotmap/slotmap.h>
#include <log/log.h>

#include <string.h>

#include "display_backend.h"

#define MEL_DISPLAY_REGISTRY_CAP 32
#define MEL_DISPLAY_EVENTS_CAP   128

typedef struct {
    u64                             stable_id;
    Mel_Display_Descriptor desc;
} Display_Slot;

typedef struct {
    u64                stable_id;
    Mel_SlotMap_Handle handle;
} Registry_Entry;

typedef struct {
    bool             initialized;
    const Mel_Alloc* alloc;
    Mel_SlotMap      slots;

    Registry_Entry entries[MEL_DISPLAY_REGISTRY_CAP];
    u32            entry_count;

    Mel_Display_Event events[MEL_DISPLAY_EVENTS_CAP];
    u32                        event_head;
    u32                        event_count;
} Registry;

static Registry g_reg;

static void icc_free(const Mel_Alloc* a, Mel_Color_Icc_Profile* icc)
{
    if (icc->data) mel_dealloc(a, (void*)icc->data);
    icc->data = NULL;
    icc->size = 0;
}

static bool icc_equal(const Mel_Color_Icc_Profile* x, const Mel_Color_Icc_Profile* y)
{
    if (x->size != y->size) return false;
    if (x->size == 0)       return true;
    return memcmp(x->data, y->data, x->size) == 0;
}

static u32 desc_changed_fields(const Mel_Display_Descriptor* a,
                               const Mel_Display_Descriptor* b)
{
    u32 f = 0;
    if (memcmp(&a->native_resolution, &b->native_resolution, sizeof a->native_resolution) != 0)
        f |= MEL_DISPLAY_FIELD_RESOLUTION;
    if (a->refresh_mode_count != b->refresh_mode_count ||
        memcmp(a->refresh_modes, b->refresh_modes,
               a->refresh_mode_count * sizeof a->refresh_modes[0]) != 0)
        f |= MEL_DISPLAY_FIELD_REFRESH;
    if (a->has_vrr != b->has_vrr || a->vrr_min_mhz != b->vrr_min_mhz || a->vrr_max_mhz != b->vrr_max_mhz)
        f |= MEL_DISPLAY_FIELD_VRR;
    if (memcmp(&a->hdr, &b->hdr, sizeof a->hdr) != 0)
        f |= MEL_DISPLAY_FIELD_HDR;
    if (!icc_equal(&a->icc_profile, &b->icc_profile))
        f |= MEL_DISPLAY_FIELD_ICC;
    if (a->scale_factor != b->scale_factor)
        f |= MEL_DISPLAY_FIELD_SCALE;
    if (a->has_position != b->has_position ||
        a->position_virtual_x != b->position_virtual_x ||
        a->position_virtual_y != b->position_virtual_y)
        f |= MEL_DISPLAY_FIELD_POSITION;
    if (a->state != b->state)
        f |= MEL_DISPLAY_FIELD_STATE;
    return f;
}

static void event_push(Mel_Display_Event ev)
{
    if (g_reg.event_count == MEL_DISPLAY_EVENTS_CAP) {
        mel_log_warn("display",
                     "event queue full (%u); dropping oldest, kind=%d",
                     MEL_DISPLAY_EVENTS_CAP, (int)ev.kind);
        g_reg.event_head = (g_reg.event_head + 1) % MEL_DISPLAY_EVENTS_CAP;
        g_reg.event_count--;
    }
    u32 tail = (g_reg.event_head + g_reg.event_count) % MEL_DISPLAY_EVENTS_CAP;
    g_reg.events[tail] = ev;
    g_reg.event_count++;
}

static Registry_Entry* entry_by_stable_id(u64 id)
{
    for (u32 i = 0; i < g_reg.entry_count; i++)
        if (g_reg.entries[i].stable_id == id) return &g_reg.entries[i];
    return NULL;
}

void mel_display_init(const Mel_Alloc* alloc)
{
    if (g_reg.initialized) return;
    g_reg.alloc = alloc ? alloc : mel_alloc_heap();
    mel_slotmap_init(&g_reg.slots, g_reg.alloc, .item_size = sizeof(Display_Slot), .initial_capacity = 8);
    g_reg.entry_count = 0;
    g_reg.event_head  = 0;
    g_reg.event_count = 0;
    g_reg.initialized = true;
    mel_display_refresh();
}

void mel_display_shutdown(void)
{
    if (!g_reg.initialized) return;
    for (u32 i = 0; i < g_reg.entry_count; i++) {
        Display_Slot* s = mel_slotmap_get(&g_reg.slots, g_reg.entries[i].handle);
        if (s) icc_free(g_reg.alloc, &s->desc.icc_profile);
    }
    mel_slotmap_free(&g_reg.slots);
    memset(&g_reg, 0, sizeof g_reg);
}

u32 mel_display_refresh(void)
{
    if (!g_reg.initialized) mel_display_init(NULL);

    Mel_Display_Raw raw[MEL_DISPLAY_REGISTRY_CAP];
    u32 n = mel_display__enumerate(g_reg.alloc, raw, MEL_DISPLAY_REGISTRY_CAP);

    bool seen[MEL_DISPLAY_REGISTRY_CAP] = {0};

    for (u32 i = 0; i < n; i++) {
        Registry_Entry* e = entry_by_stable_id(raw[i].stable_id);
        if (e) {
            seen[(u32)(e - g_reg.entries)] = true;
            Display_Slot* s = mel_slotmap_get(&g_reg.slots, e->handle);
            if (!s) { icc_free(g_reg.alloc, &raw[i].desc.icc_profile); continue; }

            u32 fields = desc_changed_fields(&s->desc, &raw[i].desc);
            if (fields == 0) {
                icc_free(g_reg.alloc, &raw[i].desc.icc_profile);
                continue;
            }
            if (fields & MEL_DISPLAY_FIELD_ICC)
                icc_free(g_reg.alloc, &s->desc.icc_profile);
            else
                icc_free(g_reg.alloc, &raw[i].desc.icc_profile);

            if (fields & MEL_DISPLAY_FIELD_ICC) {
                s->desc = raw[i].desc;
            } else {
                Mel_Color_Icc_Profile keep = s->desc.icc_profile;
                s->desc = raw[i].desc;
                s->desc.icc_profile = keep;
            }

            Mel_Display_Event_Kind kind =
                (fields == MEL_DISPLAY_FIELD_STATE)
                    ? MEL_DISPLAY_EVENT_POWER_STATE_CHANGED
                    : MEL_DISPLAY_EVENT_CONFIGURATION_CHANGED;
            event_push((Mel_Display_Event){
                .kind = kind, .display = { e->handle }, .changed_fields = fields });
            continue;
        }

        if (g_reg.entry_count == MEL_DISPLAY_REGISTRY_CAP) {
            mel_log_warn("display",
                         "registry full (%u); display id=%llu not tracked",
                         MEL_DISPLAY_REGISTRY_CAP, (unsigned long long)raw[i].stable_id);
            icc_free(g_reg.alloc, &raw[i].desc.icc_profile);
            continue;
        }
        Display_Slot slot = { .stable_id = raw[i].stable_id, .desc = raw[i].desc };
        Mel_SlotMap_Handle h = mel_slotmap_insert(&g_reg.slots, &slot);
        u32 idx = g_reg.entry_count++;
        g_reg.entries[idx] = (Registry_Entry){ .stable_id = raw[i].stable_id, .handle = h };
        seen[idx] = true;
        event_push((Mel_Display_Event){
            .kind = MEL_DISPLAY_EVENT_ADDED, .display = { h } });
    }

    for (u32 i = 0; i < g_reg.entry_count;) {
        if (seen[i]) { i++; continue; }
        Mel_SlotMap_Handle h = g_reg.entries[i].handle;
        Display_Slot* s = mel_slotmap_get(&g_reg.slots, h);
        if (s) icc_free(g_reg.alloc, &s->desc.icc_profile);
        mel_slotmap_remove(&g_reg.slots, h);
        event_push((Mel_Display_Event){
            .kind = MEL_DISPLAY_EVENT_REMOVED, .display = { h } });

        u32 last = g_reg.entry_count - 1;
        g_reg.entries[i] = g_reg.entries[last];
        seen[i]          = seen[last];
        g_reg.entry_count--;
    }

    return mel_slotmap_count(&g_reg.slots);
}

u32 mel_display_count(void)
{
    if (!g_reg.initialized) return 0;
    return mel_slotmap_count(&g_reg.slots);
}

u32 mel_display_list(Mel_Display* out, u32 cap)
{
    if (!g_reg.initialized) return 0;
    u32 n = g_reg.entry_count < cap ? g_reg.entry_count : cap;
    for (u32 i = 0; i < n; i++) out[i] = (Mel_Display){ g_reg.entries[i].handle };
    return n;
}

Mel_Display_Describe_Result mel_display_describe(Mel_Display d)
{
    Mel_Display_Describe_Result r = {0};
    if (!g_reg.initialized || !mel_slotmap_alive(&g_reg.slots, d.h)) {
        mel_log_error("display",
                      "describe on dead handle {index=%u, gen=%u}", d.h.index, d.h.generation);
        r.status = MEL_DISPLAY_STATUS_INVALID_HANDLE;
        return r;
    }
    Display_Slot* s = mel_slotmap_get(&g_reg.slots, d.h);
    r.value  = s->desc;
    r.status = MEL_DISPLAY_STATUS_OK;
    return r;
}

bool mel_display_alive(Mel_Display d)
{
    return g_reg.initialized && mel_slotmap_alive(&g_reg.slots, d.h);
}

bool mel_display_equal(Mel_Display a, Mel_Display b)
{
    return a.h.index == b.h.index && a.h.generation == b.h.generation;
}

bool mel_display__stable_id(Mel_Display d, u64* out_id)
{
    if (!mel_display_alive(d)) return false;
    Display_Slot* s = mel_slotmap_get(&g_reg.slots, d.h);
    *out_id = s->stable_id;
    return true;
}

const Mel_Display_Descriptor* mel_display__descriptor(Mel_Display d)
{
    if (!mel_display_alive(d)) return NULL;
    Display_Slot* s = mel_slotmap_get(&g_reg.slots, d.h);
    return &s->desc;
}

u32 mel_display_poll_events(Mel_Display_Event* out, u32 cap)
{
    if (!g_reg.initialized) return 0;
    u32 n = g_reg.event_count < cap ? g_reg.event_count : cap;
    for (u32 i = 0; i < n; i++)
        out[i] = g_reg.events[(g_reg.event_head + i) % MEL_DISPLAY_EVENTS_CAP];
    g_reg.event_head  = (g_reg.event_head + n) % MEL_DISPLAY_EVENTS_CAP;
    g_reg.event_count -= n;
    return n;
}
