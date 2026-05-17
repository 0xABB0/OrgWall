#pragma once

#include "ui.native.ctrl.h"
#include "str8.fwd.h"

#define MEL_NWINDOW_STYLE_TITLED         (1 << 0)
#define MEL_NWINDOW_STYLE_CLOSABLE       (1 << 1)
#define MEL_NWINDOW_STYLE_MINIATURIZABLE (1 << 2)
#define MEL_NWINDOW_STYLE_RESIZABLE      (1 << 3)
#define MEL_NWINDOW_STYLE_DEFAULT        (MEL_NWINDOW_STYLE_TITLED | MEL_NWINDOW_STYLE_CLOSABLE | MEL_NWINDOW_STYLE_MINIATURIZABLE | MEL_NWINDOW_STYLE_RESIZABLE)

typedef void (*Mel_NWindow_Close_Cb)(void* user);
typedef void (*Mel_NWindow_Resize_Cb)(f32 w, f32 h, void* user);

typedef struct {
    Mel_NCtrl base;
    str8 title;
    u32 style_flags;
    Mel_NWindow_Close_Cb on_close;
    Mel_NWindow_Resize_Cb on_resize;
    void* user_data;
} Mel_NWindow;

typedef struct {
    str8 title;
    f32 width;
    f32 height;
    u32 style_flags;
} Mel_NWindow_Opt;

void mel_nwindow_init_opt(Mel_NWindow* window, Mel_NWindow_Opt opt);
#define mel_nwindow_init(window, ...) mel_nwindow_init_opt((window), (Mel_NWindow_Opt){__VA_ARGS__})

void mel_nwindow_set_title(Mel_NWindow* window, str8 title);
void mel_nwindow_show(Mel_NWindow* window);
void mel_nwindow_close(Mel_NWindow* window);

extern const Mel_NCtrl_VTable* mel__nwindow_vtable(void);
extern void mel__nwindow_set_title_platform(Mel_NWindow* window, const char* title);
extern void mel__nwindow_show_platform(Mel_NWindow* window);
extern void mel__nwindow_close_platform(Mel_NWindow* window);
