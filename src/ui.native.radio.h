#pragma once

#include "ui.native.ctrl.h"

typedef void (*Mel_NRadio_Change_Cb)(bool selected, void* user);

typedef struct {
    Mel_NCtrl base;
    const char* text;
    i32 group_id;
    bool selected;
    Mel_NRadio_Change_Cb on_change;
    void* user_data;
} Mel_NRadio;

typedef struct {
    const char* text;
    i32 group_id;
    bool selected;
} Mel_NRadio_Opt;

void mel_nradio_init_opt(Mel_NRadio* radio, Mel_NRadio_Opt opt);
#define mel_nradio_init(radio, ...) mel_nradio_init_opt((radio), (Mel_NRadio_Opt){__VA_ARGS__})

void mel_nradio_set_text(Mel_NRadio* radio, const char* text);
void mel_nradio_set_selected(Mel_NRadio* radio, bool selected);
