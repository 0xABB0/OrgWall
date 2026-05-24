#pragma once

#include <core/types.h>
#include <string/str8.h>

#include <gui/handle.h>
#include <gui/callbacks.h>
#include <gui/layout.h>

typedef struct {
    i32   x, y, w, h;
    u32   id;
    bool  disabled;
    bool  hidden;
    void* user;
    void (*on_select)(Mel_Gui_Handle h, i32 index, void* user);
    Mel_Gui_Focus_Cb focus;
    Mel_Layoutable   layoutable;
} Mel_TabView_Opt;

Mel_Gui_Handle mel_tabview_create_opt(Mel_Gui_Handle parent, Mel_TabView_Opt opt);
#define mel_tabview_create(parent, ...) \
    mel_tabview_create_opt((parent), (Mel_TabView_Opt){__VA_ARGS__})

typedef struct {
    str8  title;
    u32   id;
    void* user;
    Mel_Layout*    layout;
    Mel_Layoutable layoutable;
} Mel_Tab_Opt;

Mel_Gui_Handle mel_tab_create_opt(Mel_Gui_Handle tabview, Mel_Tab_Opt opt);
#define mel_tab_create(tabview, ...) \
    mel_tab_create_opt((tabview), (Mel_Tab_Opt){__VA_ARGS__})

void mel_tabview_select(Mel_Gui_Handle tabview, i32 index);
i32  mel_tabview_selected(Mel_Gui_Handle tabview);
