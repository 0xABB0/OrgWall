#pragma once

#include <display/display.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    MEL_DISPLAY_FIELD_RESOLUTION = 1u << 0,
    MEL_DISPLAY_FIELD_REFRESH    = 1u << 1,
    MEL_DISPLAY_FIELD_VRR        = 1u << 2,
    MEL_DISPLAY_FIELD_HDR        = 1u << 3,
    MEL_DISPLAY_FIELD_ICC        = 1u << 4,
    MEL_DISPLAY_FIELD_SCALE      = 1u << 5,
    MEL_DISPLAY_FIELD_POSITION   = 1u << 6,
    MEL_DISPLAY_FIELD_STATE      = 1u << 7,
};

typedef enum {
    MEL_DISPLAY_EVENT_ADDED = 0,
    MEL_DISPLAY_EVENT_REMOVED,
    MEL_DISPLAY_EVENT_CONFIGURATION_CHANGED,
    MEL_DISPLAY_EVENT_POWER_STATE_CHANGED,
} Mel_Display_Event_Kind;

typedef struct {
    Mel_Display_Event_Kind kind;
    Mel_Display            display;
    u32                    changed_fields;
} Mel_Display_Event;

u32 mel_display_poll_events(Mel_Display_Event* out, u32 cap);

#ifdef __cplusplus
}
#endif
