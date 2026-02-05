#pragma once

#include "ui.native.ctrl.h"

typedef struct {
    Mel_NCtrl base;
    const char* text;
    f32 font_size;
} Mel_NLabel;

typedef struct {
    const char* text;
    f32 font_size;
} Mel_NLabel_Opt;

void mel_nlabel_init_opt(Mel_NLabel* label, Mel_NLabel_Opt opt);
#define mel_nlabel_init(label, ...) mel_nlabel_init_opt((label), (Mel_NLabel_Opt){__VA_ARGS__})

void mel_nlabel_set_text(Mel_NLabel* label, const char* text);
void mel_nlabel_set_font_size(Mel_NLabel* label, f32 size);
