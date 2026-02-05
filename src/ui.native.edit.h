#pragma once

#include "ui.native.ctrl.h"

typedef void (*Mel_NEdit_Change_Cb)(const char* text, void* user);
typedef void (*Mel_NEdit_Confirm_Cb)(const char* text, void* user);

typedef struct {
    Mel_NCtrl base;
    const char* text;
    const char* placeholder;
    Mel_NEdit_Change_Cb on_change;
    Mel_NEdit_Confirm_Cb on_confirm;
    void* user_data;
} Mel_NEdit;

typedef struct {
    const char* text;
    const char* placeholder;
} Mel_NEdit_Opt;

void mel_nedit_init_opt(Mel_NEdit* edit, Mel_NEdit_Opt opt);
#define mel_nedit_init(edit, ...) mel_nedit_init_opt((edit), (Mel_NEdit_Opt){__VA_ARGS__})

void mel_nedit_set_text(Mel_NEdit* edit, const char* text);
void mel_nedit_set_placeholder(Mel_NEdit* edit, const char* placeholder);
const char* mel_nedit_get_text(Mel_NEdit* edit);
