#pragma once

#include <core/types.h>

#include "handle.h"

/* Safe-area / window insets, in logical units like the rest of the module.
 * A typed superset: each backend fills the categories it can express natively,
 * emulates where it can, and reports honest zeros where the platform has no
 * such region (win32: the client area already excludes chrome). Apple platforms
 * give a single merged safe area; Android splits it into typed categories. */
typedef struct { i32 left, top, right, bottom; } Mel_Insets;

typedef struct {
    Mel_Insets safe_area;       /* merged "avoid" region; PAD mode applies this */
    Mel_Insets system_bars;     /* status + navigation + caption bars           */
    Mel_Insets display_cutout;  /* notch / camera housing / Dynamic Island      */
    Mel_Insets ime;             /* on-screen keyboard                           */
    Mel_Insets system_gestures; /* mandatory back/home gesture zones            */
} Mel_Frame_Insets;

/* PAD: the frame insets its content to the safe area automatically (the normal
 * app default). EDGE_TO_EDGE: the frame fills the whole surface and the app owns
 * the insets (games, full-bleed canvas, apps that react to them themselves). */
typedef enum {
    MEL_FRAME_PAD = 0,
    MEL_FRAME_EDGE_TO_EDGE,
} Mel_Inset_Mode;

typedef struct {
    void (*on_insets_changed)(Mel_Gui_Handle h, const Mel_Frame_Insets* insets, void* user);
} Mel_Gui_Insets_Cb;

/* Current insets of h's toplevel frame. Updated by the backend on rotation,
 * cutout, bar and keyboard changes; the matching on_insets_changed callback
 * fires on each change. */
Mel_Frame_Insets mel_frame_insets(Mel_Gui_Handle h);
