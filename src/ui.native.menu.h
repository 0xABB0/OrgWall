#pragma once

#include "ui.native.ctrl.h"

typedef struct Mel_NMenu {
    Mel_NCtrl base;
    const char* title;
} Mel_NMenu;

typedef struct {
    const char* title;
} Mel_NMenu_Opt;

void mel_nmenu_init_opt(Mel_NMenu* menu, Mel_NMenu_Opt opt);
#define mel_nmenu_init(menu, ...) mel_nmenu_init_opt((menu), (Mel_NMenu_Opt){__VA_ARGS__})

void mel_nmenu_set_title(Mel_NMenu* menu, const char* title);
void mel_nmenu_popup(Mel_NMenu* menu, Mel_Vec2 location, Mel_NCtrl* in_view);
