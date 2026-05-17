#pragma once

#include "ui.native.ctrl.h"
#include "str8.fwd.h"

typedef void (*Mel_NEdit_Change_Cb)(str8 text, void* user);
typedef void (*Mel_NEdit_Confirm_Cb)(str8 text, void* user);

typedef struct {
    Mel_NCtrl base;
    str8 text;
    str8 placeholder;
    Mel_NEdit_Change_Cb on_change;
    Mel_NEdit_Confirm_Cb on_confirm;
    void* user_data;
} Mel_NEdit;

typedef struct {
    str8 text;
    str8 placeholder;
} Mel_NEdit_Opt;

void mel_nedit_init_opt(Mel_NEdit* edit, Mel_NEdit_Opt opt);
#define mel_nedit_init(edit, ...) mel_nedit_init_opt((edit), (Mel_NEdit_Opt){__VA_ARGS__})

void mel_nedit_set_text(Mel_NEdit* edit, str8 text);
void mel_nedit_set_placeholder(Mel_NEdit* edit, str8 placeholder);
str8 mel_nedit_get_text(Mel_NEdit* edit);

extern const Mel_NCtrl_VTable* mel__nedit_vtable(void);
extern void mel__nedit_set_text_platform(Mel_NEdit* edit, const char* text);
extern void mel__nedit_set_placeholder_platform(Mel_NEdit* edit, const char* placeholder);
extern const char* mel__nedit_get_text_platform(Mel_NEdit* edit);
