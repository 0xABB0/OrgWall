#pragma once

#include <core/types.h>

#include <gui/handle.h>
#include <gui/callbacks.h>
#include <gui/layout.h>
#include <gui/style.h>

typedef struct
{
    Mel_Style_Surface surface;
    Mel_Style_Color   divider;
} Mel_Splitter_Style;

static inline bool mel_splitter_style_any(const Mel_Splitter_Style* s) { return mel_style_surface_any(&s->surface) || s->divider.set; }

typedef struct
{
    Mel_Style_Surface surface;
} Mel_SplitPane_Style;

static inline bool mel_splitpane_style_any(const Mel_SplitPane_Style* s) { return mel_style_surface_any(&s->surface); }

typedef enum
{
    MEL_SPLIT_HORIZONTAL = 0,
    MEL_SPLIT_VERTICAL,
} Mel_Split_Orientation;

typedef struct
{
    Mel_Split_Orientation orientation;
    i32                   x, y, w, h;
    u32                   id;
    bool                  disabled;
    bool                  hidden;
    void*                 user;
    Mel_Gui_Focus_Cb      focus;
    Mel_Layoutable        layoutable;
    Mel_Splitter_Style    style;
} Mel_Splitter_Opt;

Mel_Gui_Handle mel_splitter_create_opt(Mel_Gui_Handle parent, Mel_Splitter_Opt opt);
#define mel_splitter_create(parent, ...) mel_splitter_create_opt((parent), (Mel_Splitter_Opt){ __VA_ARGS__ })

typedef struct
{
    i32                 min_size;
    i32                 initial_size;
    u32                 id;
    void*               user;
    Mel_Layout*         layout;
    Mel_Layoutable      layoutable;
    Mel_SplitPane_Style style;
} Mel_SplitPane_Opt;

Mel_Gui_Handle mel_splitpane_create_opt(Mel_Gui_Handle splitter, Mel_SplitPane_Opt opt);
#define mel_splitpane_create(splitter, ...) mel_splitpane_create_opt((splitter), (Mel_SplitPane_Opt){ __VA_ARGS__ })

void mel_splitter_set_style_opt(Mel_Gui_Handle h, Mel_Splitter_Style style);
#define mel_splitter_set_style(h, ...) mel_splitter_set_style_opt((h), (Mel_Splitter_Style){ __VA_ARGS__ })

void mel_splitpane_set_style_opt(Mel_Gui_Handle h, Mel_SplitPane_Style style);
#define mel_splitpane_set_style(h, ...) mel_splitpane_set_style_opt((h), (Mel_SplitPane_Style){ __VA_ARGS__ })
