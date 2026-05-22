#include "win32.h"

static LRESULT CALLBACK slider_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                        UINT_PTR id, DWORD_PTR ref)
{
    (void)ref;
    Mel_Gui_Handle h = mel_gui__win32_handle_of(hwnd);

    if (msg == MEL_REFLECT(WM_HSCROLL)) {
        i32             val  = (i32)SendMessageW(hwnd, TBM_GETPOS, 0, 0);
        Mel_Gui_Widget* w    = mel_gui__get(h);
        Mel_Gui_Slider_Impl* impl = w ? (Mel_Gui_Slider_Impl*)w->impl : NULL;
        if (impl) {
            impl->value = val;
            if (impl->on_.on_value_changed) {
                impl->on_.on_value_changed(h, val, w->user);
            }
        }
        return 0;
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, slider_subclass, id);
    if (mel_gui__win32_subclass_common(hwnd, msg, wp, lp, h)) return 0;
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void mel_gui__backend_slider_create(Mel_Gui_Widget* w, str8 text)
{
    (void)text;
    HWND parent = mel_gui__win32_parent_hwnd(w);
    if (!parent) return;

    HWND hwnd = CreateWindowExW(0, TRACKBAR_CLASSW, NULL,
        mel_gui__win32_child_style(w) | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS,
        w->x, w->y, w->width, w->height, parent, NULL, current_hinst, NULL);

    w->native = hwnd;
    if (!hwnd) return;

    mel_gui__win32_bind(hwnd, w->self);

    Mel_Gui_Slider_Impl* impl = (Mel_Gui_Slider_Impl*)w->impl;
    if (impl) {
        SendMessageW(hwnd, TBM_SETRANGEMIN, FALSE, impl->min_value);
        SendMessageW(hwnd, TBM_SETRANGEMAX, FALSE, impl->max_value);
        SendMessageW(hwnd, TBM_SETPOS,      TRUE,  impl->value);
    }

    SetWindowSubclass(hwnd, slider_subclass, 1, 0);
}

i32 mel_gui__backend_slider_value(Mel_Gui_Widget* w)
{
    if (!w || !w->native) return 0;
    return (i32)SendMessageW((HWND)w->native, TBM_GETPOS, 0, 0);
}

void mel_gui__backend_slider_set_value(Mel_Gui_Widget* w, i32 value)
{
    if (w && w->native) {
        SendMessageW((HWND)w->native, TBM_SETPOS, TRUE, value);
    }
}
