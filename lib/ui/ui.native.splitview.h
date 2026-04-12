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

extern const Mel_NCtrl_VTable* mel__nsplitview_vtable(void);
extern void mel__nsplitview_set_divider_platform(Mel_NSplitView* sv, f32 position);
