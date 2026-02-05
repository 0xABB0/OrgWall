#pragma once

#include "ui.native.ctrl.h"

typedef void (*Mel_NTextView_Change_Cb)(void* user);

typedef struct {
    Mel_NCtrl base;
    bool editable;
    Mel_NTextView_Change_Cb on_change;
    void* user_data;
} Mel_NTextView;

typedef struct {
    bool editable;
} Mel_NTextView_Opt;

void mel_ntextview_init_opt(Mel_NTextView* tv, Mel_NTextView_Opt opt);
#define mel_ntextview_init(tv, ...) mel_ntextview_init_opt((tv), (Mel_NTextView_Opt){__VA_ARGS__})

void mel_ntextview_set_text(Mel_NTextView* tv, const char* text);
const char* mel_ntextview_get_text(Mel_NTextView* tv);
void mel_ntextview_set_editable(Mel_NTextView* tv, bool editable);
