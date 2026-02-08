#pragma once

#include "ui.native.ctrl.h"

typedef void (*Mel_NMenuItem_Action_Cb)(void* user);

typedef struct Mel_NMenu Mel_NMenu;

typedef struct {
    Mel_NCtrl base;
    const char* title;
    const char* key_equivalent;
    Mel_NMenu* submenu;
    Mel_NMenuItem_Action_Cb on_action;
    void* user_data;
} Mel_NMenuItem;

typedef struct {
    const char* title;
    const char* key_equivalent;
} Mel_NMenuItem_Opt;

void mel_nmenuitem_init_opt(Mel_NMenuItem* item, Mel_NMenuItem_Opt opt);
#define mel_nmenuitem_init(item, ...) mel_nmenuitem_init_opt((item), (Mel_NMenuItem_Opt){__VA_ARGS__})

void mel_nmenuitem_set_title(Mel_NMenuItem* item, const char* title);
void mel_nmenuitem_set_submenu(Mel_NMenuItem* item, Mel_NMenu* submenu);

extern const Mel_NCtrl_VTable* mel__nmenuitem_vtable(void);
extern void mel__nmenuitem_set_title_platform(Mel_NMenuItem* item, const char* title);
extern void mel__nmenuitem_set_submenu_platform(Mel_NMenuItem* item, Mel_NMenu* submenu);
