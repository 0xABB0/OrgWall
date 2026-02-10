#pragma once

#include "ui.native.ctrl.h"
#include "string.str8.fwd.h"

typedef struct {
    Mel_NCtrl base;
    str8 text;
    f32 font_size;
} Mel_NLabel;

typedef struct {
    str8 text;
    f32 font_size;
} Mel_NLabel_Opt;

void mel_nlabel_init_opt(Mel_NLabel* label, Mel_NLabel_Opt opt);
#define mel_nlabel_init(label, ...) mel_nlabel_init_opt((label), (Mel_NLabel_Opt){__VA_ARGS__})

void mel_nlabel_set_text(Mel_NLabel* label, str8 text);
void mel_nlabel_set_font_size(Mel_NLabel* label, f32 size);

extern const Mel_NCtrl_VTable* mel__nlabel_vtable(void);
extern void mel__nlabel_set_text_platform(Mel_NLabel* label, const char* text);
extern void mel__nlabel_set_font_size_platform(Mel_NLabel* label, f32 size);
