#pragma once

#include "ui.native.ctrl.h"

typedef struct {
    Mel_NCtrl base;
} Mel_NPanel;

typedef struct {
    f32 width;
    f32 height;
} Mel_NPanel_Opt;

void mel_npanel_init_opt(Mel_NPanel* panel, Mel_NPanel_Opt opt);
#define mel_npanel_init(panel, ...) mel_npanel_init_opt((panel), (Mel_NPanel_Opt){__VA_ARGS__})
