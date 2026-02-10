#pragma once

#include "ui.native.ctrl.h"
#include "string.str8.fwd.h"

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

void mel_ntextview_set_text(Mel_NTextView* tv, str8 text);
str8 mel_ntextview_get_text(Mel_NTextView* tv);
void mel_ntextview_set_editable(Mel_NTextView* tv, bool editable);

extern const Mel_NCtrl_VTable* mel__ntextview_vtable(void);
extern void mel__ntextview_set_text_platform(Mel_NTextView* tv, const char* text);
extern const char* mel__ntextview_get_text_platform(Mel_NTextView* tv);
extern void mel__ntextview_set_editable_platform(Mel_NTextView* tv, bool editable);
