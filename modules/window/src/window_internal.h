#pragma once

#include <core/types.h>
#include <collection/slotmap.h>
#include <allocator/allocator.h>
#include <reactor/reactor.h>

#include <window/window.h>
#include <window/state.h>

typedef struct Mel_Window_Node Mel_Window_Node;

typedef struct
{
    bool (*set_min_size)(Mel_Window_Node* n, i32 w, i32 h);
    bool (*set_max_size)(Mel_Window_Node* n, i32 w, i32 h);
    bool (*set_aspect)(Mel_Window_Node* n, f32 min_ratio, f32 max_ratio);
    bool (*set_fullscreen)(Mel_Window_Node* n, u32 flags);
    bool (*set_fullscreen_mode)(Mel_Window_Node* n, Mel_Window_Video_Mode mode);
    bool (*get_fullscreen_mode)(Mel_Window_Node* n, Mel_Window_Video_Mode* out);
    bool (*set_opacity)(Mel_Window_Node* n, f32 opacity);
    bool (*set_always_on_top)(Mel_Window_Node* n, bool on);
    bool (*set_borderless)(Mel_Window_Node* n, bool borderless);
    bool (*set_resizable)(Mel_Window_Node* n, bool resizable);
    bool (*set_icon)(Mel_Window_Node* n, const u8* rgba, i32 w, i32 h);
    bool (*set_modal)(Mel_Window_Node* n, bool modal);
    bool (*set_parent)(Mel_Window_Node* n, Mel_Window_Node* parent);
    bool (*set_shape)(Mel_Window_Node* n, const u8* alpha, i32 w, i32 h);
    bool (*set_mouse_grab)(Mel_Window_Node* n, bool grab);
    bool (*set_keyboard_grab)(Mel_Window_Node* n, bool grab);
    bool (*set_mouse_rect)(Mel_Window_Node* n, Mel_Window_Rect rect);
    bool (*set_progress_state)(Mel_Window_Node* n, u32 state);
    bool (*set_progress_value)(Mel_Window_Node* n, f32 value);
    bool (*safe_area)(Mel_Window_Node* n, Mel_Window_Rect* out);
    bool (*pixel_format)(Mel_Window_Node* n, u32* out_flags);
    u64  (*native_id)(Mel_Window_Node* n);
    bool (*maximize)(Mel_Window_Node* n);
    bool (*minimize)(Mel_Window_Node* n);
    bool (*restore)(Mel_Window_Node* n);
    bool (*raise)(Mel_Window_Node* n);
    bool (*flash)(Mel_Window_Node* n, u32 flags);
    bool (*get_surface)(Mel_Window_Node* n, Mel_Window_Surface* out);
    bool (*present_surface)(Mel_Window_Node* n);
    bool (*icc_profile)(Mel_Window_Node* n, Mel_Window_Icc_Profile* out);
    u32  (*live_flags)(Mel_Window_Node* n);
} Mel_Window_Backend_Ops;

struct Mel_Window_Node
{
    Mel_Window self;
    void*      native;
    void*      content;
    void*      user;
    i32        x, y, w, h;
    i32        point_w, point_h;
    f32        scale;
    bool       borrowed;

    i32        min_w, min_h;
    i32        max_w, max_h;
    f32        aspect_min, aspect_max;
    f32        opacity;
    u32        fullscreen_flags;
    u32        progress_state;
    f32        progress_value;
    bool       always_on_top;
    bool       borderless;
    bool       resizable;
    bool       mouse_grab;
    bool       keyboard_grab;
    bool       modal;
    bool       transparent;
    bool       have_mouse_rect;
    Mel_Window_Rect mouse_rect;

    Mel_Window          parent;
    Mel_Window_Hit_Test hit_test;
    void*               hit_test_user;

    void* surface_pixels;
    i32   surface_w, surface_h;
    i32   surface_stride;
    u32   surface_format;

    const Mel_Window_Backend_Ops* ops;

    Mel_Window_Lifecycle_Cb lifecycle;
    Mel_Window_Display_Cb   display;
    Mel_Window_App_Cb       app;
    Mel_Window_Input_Cb     input;
    Mel_Window_Backing_Cb   backing;
};

const Mel_Alloc* mel_window__alloc(void);
Mel_Reactor*     mel_window__reactor(void);
Mel_Window_Node* mel_window__node(Mel_Window w);

u32              mel_window__node_count(void);
Mel_Window_Node* mel_window__node_dense(u32 dense_index);

i32  mel_window__count_inc(void);
i32  mel_window__count_dec(void);
void mel_window__resized(Mel_Window w, i32 pixel_w, i32 pixel_h);
void mel_window__closed(Mel_Window w);

bool mel_window__backend_init(void);
void mel_window__backend_create(Mel_Window_Node* n, const Mel_Window_Opt* opt);
void mel_window__backend_destroy(Mel_Window_Node* n);

const Mel_Window_Backend_Ops* mel_window__backend_ops(void);
