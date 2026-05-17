#pragma once

#include "ui.native.ctrl.h"
#include "str8.fwd.h"

typedef struct Mel_NMenu {
    Mel_NCtrl base;
    str8 title;
} Mel_NMenu;

typedef struct {
    str8 title;
} Mel_NMenu_Opt;

void mel_nmenu_init_opt(Mel_NMenu* menu, Mel_NMenu_Opt opt);
#define mel_nmenu_init(menu, ...) mel_nmenu_init_opt((menu), (Mel_NMenu_Opt){__VA_ARGS__})

void mel_nmenu_set_title(Mel_NMenu* menu, str8 title);
void mel_nmenu_popup(Mel_NMenu* menu, Mel_Vec2 location, Mel_NCtrl* in_view);

extern const Mel_NCtrl_VTable* mel__nmenu_vtable(void);
extern void mel__nmenu_set_title_platform(Mel_NMenu* menu, const char* title);
extern void mel__nmenu_popup_platform(Mel_NMenu* menu, Mel_Vec2 location, Mel_NCtrl* in_view);
