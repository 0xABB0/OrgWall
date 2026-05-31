#pragma once

#include <core/types.h>
#include <collection.slotmap/slotmap.h>
#include <allocator/allocator.h>
#include <reactor/reactor.h>

#include <window/window.h>

typedef struct {
    Mel_Window self;
    void*      native;
    void*      content;
    void*       user;
    i32         x, y, w, h;
    i32         point_w, point_h;
    f32         scale;
    Mel_Display current_display;
    bool        borrowed;

    Mel_Window_Lifecycle_Cb lifecycle;
    Mel_Window_Display_Cb   display;
    Mel_Window_App_Cb       app;
    Mel_Window_Input_Cb     input;
    Mel_Window_Backing_Cb   backing;
} Mel_Window_Node;

const Mel_Alloc* mel_window__alloc(void);
Mel_Reactor*     mel_window__reactor(void);
Mel_Window_Node* mel_window__node(Mel_Window w);

i32  mel_window__count_inc(void);
i32  mel_window__count_dec(void);
void mel_window__resized(Mel_Window w, i32 pixel_w, i32 pixel_h);
void mel_window__closed (Mel_Window w);

bool mel_window__backend_init(void);
void mel_window__backend_create(Mel_Window_Node* n, const Mel_Window_Opt* opt);
void mel_window__backend_destroy(Mel_Window_Node* n);
