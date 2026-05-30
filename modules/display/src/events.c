#include "events_internal.h"

#include <log/log.h>

#include <string.h>

#define MEL_DISPLAY_EVENTS_CAP 128

static struct {
    Mel_Display_Event events[MEL_DISPLAY_EVENTS_CAP];
    u32               head;
    u32               count;
} g_events;

static bool icc_equal(const Mel_Color_Icc_Profile* x, const Mel_Color_Icc_Profile* y)
{
    if (x->size != y->size) return false;
    if (x->size == 0)       return true;
    return memcmp(x->data, y->data, x->size) == 0;
}

u32 mel_display_events__changed_fields(const Mel_Display_Descriptor* a,
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

void mel_display_events__reset(void)
{
    g_events.head  = 0;
    g_events.count = 0;
}

void mel_display_events__emit(Mel_Display_Event ev)
{
    if (g_events.count == MEL_DISPLAY_EVENTS_CAP) {
        mel_log_warn("display",
                     "event queue full (%u); dropping oldest, kind=%d",
                     MEL_DISPLAY_EVENTS_CAP, (int)ev.kind);
        g_events.head = (g_events.head + 1) % MEL_DISPLAY_EVENTS_CAP;
        g_events.count--;
    }
    u32 tail = (g_events.head + g_events.count) % MEL_DISPLAY_EVENTS_CAP;
    g_events.events[tail] = ev;
    g_events.count++;
}

u32 mel_display_poll_events(Mel_Display_Event* out, u32 cap)
{
    u32 n = g_events.count < cap ? g_events.count : cap;
    for (u32 i = 0; i < n; i++)
        out[i] = g_events.events[(g_events.head + i) % MEL_DISPLAY_EVENTS_CAP];
    g_events.head  = (g_events.head + n) % MEL_DISPLAY_EVENTS_CAP;
    g_events.count -= n;
    return n;
}
