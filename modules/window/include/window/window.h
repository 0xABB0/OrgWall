#pragma once

#include <core/types.h>
#include <string/str8.h>
#include <reactor/reactor.h>
#include <display/display.h>

typedef struct {
    u32 index;
    u32 generation;
} Mel_Window;

#define MEL_WINDOW_NONE ((Mel_Window){0})

static inline bool mel_window_is_none(Mel_Window w)
{
    return w.index == 0 && w.generation == 0;
}

static inline bool mel_window_eq(Mel_Window a, Mel_Window b)
{
    return a.index == b.index && a.generation == b.generation;
}

typedef struct {
    void (*on_resize)       (Mel_Window w, i32 pixel_w, i32 pixel_h, void* user);
    void (*on_move)         (Mel_Window w, i32 x, i32 y, void* user);
    bool (*on_close_request)(Mel_Window w, void* user);
    void (*on_closed)       (Mel_Window w, void* user);
    void (*on_focus_in)     (Mel_Window w, void* user);
    void (*on_focus_out)    (Mel_Window w, void* user);
} Mel_Window_Lifecycle_Cb;

typedef struct {
    void (*on_scale_changed)      (Mel_Window w, f32 scale, void* user);
    void (*on_display_migrated)   (Mel_Window w, Mel_Display from, Mel_Display to, void* user);
    void (*on_hdr_changed)        (Mel_Window w, void* user);
    void (*on_orientation_changed)(Mel_Window w, void* user);
} Mel_Window_Display_Cb;

typedef struct {
    void (*on_foreground)(Mel_Window w, void* user);
    void (*on_background)(Mel_Window w, void* user);
    void (*on_occluded)  (Mel_Window w, bool occluded, void* user);
} Mel_Window_App_Cb;

typedef struct {
    void (*on_pointer_down)(Mel_Window w, i32 x, i32 y, void* user);
    void (*on_pointer_up)  (Mel_Window w, i32 x, i32 y, void* user);
    void (*on_pointer_move)(Mel_Window w, i32 x, i32 y, void* user);
    void (*on_key_down)    (Mel_Window w, u32 key, void* user);
    void (*on_key_up)      (Mel_Window w, u32 key, void* user);
} Mel_Window_Input_Cb;

typedef struct {
    void (*on_backing_lost)    (Mel_Window w, void* user);
    void (*on_content_replaced)(Mel_Window w, void* new_native, void* user);
} Mel_Window_Backing_Cb;

typedef struct {
    str8 title;
    i32  x, y, w, h;
    i32  min_w, min_h;
    i32  max_w, max_h;
    bool not_resizable;
    bool undecorated;
    bool not_closable;
    bool start_hidden;
    void* content_native;
    void* user;

    Mel_Window_Lifecycle_Cb lifecycle;
    Mel_Window_Display_Cb   display;
    Mel_Window_App_Cb       app;
    Mel_Window_Input_Cb     input;
    Mel_Window_Backing_Cb   backing;
} Mel_Window_Opt;

void mel_window_init    (Mel_Reactor* reactor);
void mel_window_shutdown (void);
bool mel_window_alive   (Mel_Window w);

Mel_Window mel_window_create_opt(Mel_Window_Opt opt);
#define mel_window_create(...) mel_window_create_opt((Mel_Window_Opt){__VA_ARGS__})

void mel_window_destroy    (Mel_Window w);
void mel_window_set_title  (Mel_Window w, str8 title);
void mel_window_set_bounds (Mel_Window w, i32 x, i32 y, i32 width, i32 height);
void mel_window_set_visible(Mel_Window w, bool visible);
void mel_window_set_focus  (Mel_Window w);
void mel_window_refresh    (Mel_Window w);

void* mel_window_content_native(Mel_Window w);
void* mel_window_native        (Mel_Window w);

bool mel_window_should_close(Mel_Window w);

void mel_window_pixel_extent(Mel_Window w, i32* out_w, i32* out_h);
void mel_window_point_extent(Mel_Window w, i32* out_w, i32* out_h);
f32  mel_window_scale       (Mel_Window w);
