#pragma once

#include "ui.native.ctrl.h"

typedef struct {
    Mel_NCtrl base;
    i32 orientation;
    f32 divider_position;
} Mel_NSplitView;

typedef struct {
    i32 orientation;
    f32 divider_position;
} Mel_NSplitView_Opt;

void mel_nsplitview_init_opt(Mel_NSplitView* sv, Mel_NSplitView_Opt opt);
#define mel_nsplitview_init(sv, ...) mel_nsplitview_init_opt((sv), (Mel_NSplitView_Opt){__VA_ARGS__})

void mel_nsplitview_set_divider_position(Mel_NSplitView* sv, f32 position);
