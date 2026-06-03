#include "events_internal.h"

#include <string.h>

static bool icc_equal(const Mel_Color_Icc_Profile* x, const Mel_Color_Icc_Profile* y)
{
    if (x->size != y->size)
        return false;
    if (x->size == 0)
        return true;
    return memcmp(x->data, y->data, x->size) == 0;
}

u32 mel_display_events__changed_fields(const Mel_Display_Descriptor* a, const Mel_Display_Descriptor* b)
{
    u32 f = 0;
    if (memcmp(&a->native_resolution, &b->native_resolution, sizeof a->native_resolution) != 0)
        f |= MEL_DISPLAY_FIELD_RESOLUTION;
    if (a->refresh_mode_count != b->refresh_mode_count || memcmp(a->refresh_modes, b->refresh_modes, a->refresh_mode_count * sizeof a->refresh_modes[0]) != 0)
        f |= MEL_DISPLAY_FIELD_REFRESH;
    if (a->has_vrr != b->has_vrr || a->vrr_min_mhz != b->vrr_min_mhz || a->vrr_max_mhz != b->vrr_max_mhz)
        f |= MEL_DISPLAY_FIELD_VRR;
    if (memcmp(&a->hdr, &b->hdr, sizeof a->hdr) != 0)
        f |= MEL_DISPLAY_FIELD_HDR;
    if (!icc_equal(&a->icc_profile, &b->icc_profile))
        f |= MEL_DISPLAY_FIELD_ICC;
    if (a->scale_factor != b->scale_factor)
        f |= MEL_DISPLAY_FIELD_SCALE;
    if (a->has_position != b->has_position || a->position_virtual_x != b->position_virtual_x || a->position_virtual_y != b->position_virtual_y)
        f |= MEL_DISPLAY_FIELD_POSITION;
    if (a->state != b->state)
        f |= MEL_DISPLAY_FIELD_STATE;
    return f;
}
