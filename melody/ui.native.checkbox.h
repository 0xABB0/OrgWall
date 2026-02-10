#pragma once

#include "ui.native.ctrl.h"
#include "string.str8.fwd.h"

typedef void (*Mel_NCheckbox_Change_Cb)(bool checked, void* user);

typedef struct {
    Mel_NCtrl base;
    str8 text;
    bool checked;
    Mel_NCheckbox_Change_Cb on_change;
    void* user_data;
} Mel_NCheckbox;

typedef struct {
    str8 text;
    bool checked;
} Mel_NCheckbox_Opt;

void mel_ncheckbox_init_opt(Mel_NCheckbox* checkbox, Mel_NCheckbox_Opt opt);
#define mel_ncheckbox_init(checkbox, ...) mel_ncheckbox_init_opt((checkbox), (Mel_NCheckbox_Opt){__VA_ARGS__})

void mel_ncheckbox_set_text(Mel_NCheckbox* checkbox, str8 text);
void mel_ncheckbox_set_checked(Mel_NCheckbox* checkbox, bool checked);

extern const Mel_NCtrl_VTable* mel__ncheckbox_vtable(void);
extern void mel__ncheckbox_set_text_platform(Mel_NCheckbox* checkbox, const char* text);
extern void mel__ncheckbox_set_checked_platform(Mel_NCheckbox* checkbox, bool checked);
