#pragma once

#include <core/types.h>
#include <string/str8.h>

#include <gui/handle.h>
#include <gui/callbacks.h>
#include <gui/layout.h>
#include <gui/style.h>

typedef struct
{
    Mel_Style_Surface surface;
} Mel_TabView_Style;

static inline bool mel_tabview_style_any(const Mel_TabView_Style* s) { return mel_style_surface_any(&s->surface); }

typedef struct
{
    Mel_Style_Surface surface;
} Mel_Tab_Style;

static inline bool mel_tab_style_any(const Mel_Tab_Style* s) { return mel_style_surface_any(&s->surface); }

typedef struct
{
    i32   x, y, w, h;
    u32   id;
    bool  disabled;
    bool  hidden;
    void* user;
    void (*on_select)(Mel_Gui_Handle h, i32 index, void* user);
    Mel_Gui_Focus_Cb  focus;
    Mel_Layoutable    layoutable;
    Mel_TabView_Style style;
} Mel_TabView_Opt;

Mel_Gui_Handle mel_tabview_create_opt(Mel_Gui_Handle parent, Mel_TabView_Opt opt);
#define mel_tabview_create(parent, ...) mel_tabview_create_opt((parent), (Mel_TabView_Opt){ __VA_ARGS__ })

typedef struct
{
    str8           title;
    u32            id;
    void*          user;
    Mel_Layout*    layout;
    Mel_Layoutable layoutable;
    Mel_Tab_Style  style;
} Mel_Tab_Opt;

Mel_Gui_Handle mel_tab_create_opt(Mel_Gui_Handle tabview, Mel_Tab_Opt opt);
#define mel_tab_create(tabview, ...) mel_tab_create_opt((tabview), (Mel_Tab_Opt){ __VA_ARGS__ })

void mel_tabview_select(Mel_Gui_Handle tabview, i32 index);
i32  mel_tabview_selected(Mel_Gui_Handle tabview);

void mel_tabview_set_style_opt(Mel_Gui_Handle h, Mel_TabView_Style style);
#define mel_tabview_set_style(h, ...) mel_tabview_set_style_opt((h), (Mel_TabView_Style){ __VA_ARGS__ })

void mel_tab_set_style_opt(Mel_Gui_Handle h, Mel_Tab_Style style);
#define mel_tab_set_style(h, ...) mel_tab_set_style_opt((h), (Mel_Tab_Style){ __VA_ARGS__ })
