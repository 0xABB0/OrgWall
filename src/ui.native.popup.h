#pragma once

#include "ui.native.ctrl.h"

#define MEL_NPOPUP_SIDE_BOTTOM  0
#define MEL_NPOPUP_SIDE_TOP     1
#define MEL_NPOPUP_SIDE_LEFT    2
#define MEL_NPOPUP_SIDE_RIGHT   3

typedef struct {
    Mel_NCtrl base;
    i32 side;
} Mel_NPopup;

typedef struct {
    f32 width;
    f32 height;
    i32 side;
} Mel_NPopup_Opt;

void mel_npopup_init_opt(Mel_NPopup* popup, Mel_NPopup_Opt opt);
#define mel_npopup_init(popup, ...) mel_npopup_init_opt((popup), (Mel_NPopup_Opt){__VA_ARGS__})

void mel_npopup_show_relative_to(Mel_NPopup* popup, Mel_NCtrl* anchor);
void mel_npopup_close(Mel_NPopup* popup);
