#include "ui.native.window.h"

extern const Mel_NCtrl_VTable* mel__nwindow_vtable_osx(void);
extern void mel__nwindow_set_title_platform(Mel_NWindow* window, const char* title);
extern void mel__nwindow_show_platform(Mel_NWindow* window);
extern void mel__nwindow_close_platform(Mel_NWindow* window);

void mel_nwindow_init_opt(Mel_NWindow* window, Mel_NWindow_Opt opt)
{
    assert(window != nullptr);

    mel_nctrl_init(&window->base, mel__nwindow_vtable_osx());

    window->title       = opt.title ? opt.title : "Window";
    window->style_flags = opt.style_flags ? opt.style_flags : MEL_NWINDOW_STYLE_DEFAULT;
    window->on_close    = nullptr;
    window->on_resize   = nullptr;
    window->user_data   = nullptr;

    f32 w = opt.width  > 0 ? opt.width  : 800.0f;
    f32 h = opt.height > 0 ? opt.height : 600.0f;
    window->base.size = mel_vec2(w, h);
}

void mel_nwindow_set_title(Mel_NWindow* window, const char* title)
{
    assert(window != nullptr);
    window->title = title;
    if (window->base.backing)
        mel__nwindow_set_title_platform(window, title);
}

void mel_nwindow_show(Mel_NWindow* window)
{
    assert(window != nullptr);
    mel__nwindow_show_platform(window);
}

void mel_nwindow_close(Mel_NWindow* window)
{
    assert(window != nullptr);
    mel__nwindow_close_platform(window);
}
