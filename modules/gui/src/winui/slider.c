#include "win32.h"

static LRESULT CALLBACK slider_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                        UINT_PTR id, DWORD_PTR ref)
{
    (void)ref;
    if (msg == MEL_REFLECT(WM_HSCROLL)) {
        Mel_Win32_Slider* sl = (Mel_Win32_Slider*)mel_gui__win32_ctl(hwnd);
        if (sl && sl->on_.on_value_changed) {
            i32 val = (i32)SendMessageW(hwnd, TBM_GETPOS, 0, 0);
            sl->on_.on_value_changed(sl->base.handle, val, mel_gui_user(sl->base.handle));
        }
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, slider_subclass, id);
        mel_gui__win32_free_ctl(hwnd);
        return DefSubclassProc(hwnd, msg, wp, lp);
    }
    if (mel_gui__win32_subclass_common(hwnd, msg, wp, lp)) return 0;
    return DefSubclassProc(hwnd, msg, wp, lp);
}

Mel_Gui_Handle mel_slider_create_opt(Mel_Gui_Handle parent, Mel_Slider_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    HWND par = mel_gui__win32_parent_hwnd(n);
    if (!par) return h;

    i32 max_value = (o.max_value > o.min_value) ? o.max_value : o.min_value + 100;

    HWND hwnd = CreateWindowExW(0, TRACKBAR_CLASSW, NULL,
        mel_gui__win32_child_style(n, o.disabled) | WS_TABSTOP | TBS_HORZ | TBS_AUTOTICKS,
        n->x, n->y, n->width, n->height, par, NULL, current_hinst, NULL);
    n->native = hwnd;
    if (!hwnd) return h;

    Mel_Win32_Slider* sl = (Mel_Win32_Slider*)mel_gui__win32_alloc_ctl(hwnd, sizeof *sl, h);
    if (sl) {
        sl->base.focus    = o.focus;
        sl->base.keyboard = o.keyboard;
        sl->on_           = o.on_;
    }
    SendMessageW(hwnd, TBM_SETRANGEMIN, FALSE, o.min_value);
    SendMessageW(hwnd, TBM_SETRANGEMAX, FALSE, max_value);
    SendMessageW(hwnd, TBM_SETPOS,      TRUE,  o.value);
    SetWindowSubclass(hwnd, slider_subclass, 1, 0);
    return h;
}

i32 mel_slider_value(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native) return 0;
    return (i32)SendMessageW((HWND)n->native, TBM_GETPOS, 0, 0);
}

void mel_slider_set_value(Mel_Gui_Handle h, i32 value)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (n && n->native) SendMessageW((HWND)n->native, TBM_SETPOS, TRUE, value);
}
