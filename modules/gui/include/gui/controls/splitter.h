#pragma once

#include <core/types.h>

#include <gui/handle.h>
#include <gui/callbacks.h>
#include <gui/layout.h>

typedef enum {
    MEL_SPLIT_HORIZONTAL = 0,
    MEL_SPLIT_VERTICAL,
} Mel_Split_Orientation;

typedef struct {
    Mel_Split_Orientation orientation;
    i32   x, y, w, h;
    u32   id;
    bool  disabled;
    bool  hidden;
    void* user;
    Mel_Gui_Focus_Cb focus;
    Mel_Layoutable   layoutable;
} Mel_Splitter_Opt;

Mel_Gui_Handle mel_splitter_create_opt(Mel_Gui_Handle parent, Mel_Splitter_Opt opt);
#define mel_splitter_create(parent, ...) \
    mel_splitter_create_opt((parent), (Mel_Splitter_Opt){__VA_ARGS__})

typedef struct {
    i32   min_size;
    i32   initial_size;
    u32   id;
    void* user;
    Mel_Layout*    layout;
    Mel_Layoutable layoutable;
} Mel_SplitPane_Opt;

Mel_Gui_Handle mel_splitpane_create_opt(Mel_Gui_Handle splitter, Mel_SplitPane_Opt opt);
#define mel_splitpane_create(splitter, ...) \
    mel_splitpane_create_opt((splitter), (Mel_SplitPane_Opt){__VA_ARGS__})
