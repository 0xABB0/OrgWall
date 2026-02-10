#pragma once

#include "ui.native.ctrl.h"
#include "string.str8.fwd.h"

typedef void (*Mel_NButton_Click_Cb)(void* user);

typedef struct {
    Mel_NCtrl base;
    str8 text;
    Mel_NButton_Click_Cb on_click;
    void* user_data;
} Mel_NButton;

typedef struct {
    str8 text;
} Mel_NButton_Opt;

void mel_nbutton_init_opt(Mel_NButton* button, Mel_NButton_Opt opt);
#define mel_nbutton_init(button, ...) mel_nbutton_init_opt((button), (Mel_NButton_Opt){__VA_ARGS__})

void mel_nbutton_set_text(Mel_NButton* button, str8 text);

extern const Mel_NCtrl_VTable* mel__nbutton_vtable(void);
extern void mel__nbutton_set_text_platform(Mel_NButton* button, const char* text);
