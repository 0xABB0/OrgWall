#pragma once

#include "ui.native.ctrl.h"

typedef void (*Mel_NCombo_Select_Cb)(i32 index, void* user);

typedef struct {
    Mel_NCtrl base;
    const char** items;
    i32 item_count;
    i32 selected;
    Mel_NCombo_Select_Cb on_select;
    void* user_data;
} Mel_NCombo;

typedef struct {
    const char** items;
    i32 item_count;
    i32 selected;
} Mel_NCombo_Opt;

void mel_ncombo_init_opt(Mel_NCombo* combo, Mel_NCombo_Opt opt);
#define mel_ncombo_init(combo, ...) mel_ncombo_init_opt((combo), (Mel_NCombo_Opt){__VA_ARGS__})

void mel_ncombo_set_items(Mel_NCombo* combo, const char** items, i32 count);
void mel_ncombo_set_selected(Mel_NCombo* combo, i32 index);
