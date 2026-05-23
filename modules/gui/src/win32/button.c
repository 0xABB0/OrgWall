#include "win32.h"

static LRESULT CALLBACK button_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                        UINT_PTR id, DWORD_PTR ref)
{
    (void)ref;
    if (msg == MEL_REFLECT(WM_COMMAND)) {
        if (HIWORD(wp) == BN_CLICKED) {
            Mel_Win32_Button* b = (Mel_Win32_Button*)mel_gui__win32_ctl(hwnd);
            if (b && b->pointer.on_click) {
                b->pointer.on_click(b->base.handle, mel_gui_user(b->base.handle));
            }
        }
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, button_subclass, id);
        mel_gui__win32_free_ctl(hwnd);
        return DefSubclassProc(hwnd, msg, wp, lp);
    }
    if (mel_gui__win32_subclass_common(hwnd, msg, wp, lp)) return 0;
    return DefSubclassProc(hwnd, msg, wp, lp);
}

Mel_Gui_Handle mel_button_create_opt(Mel_Gui_Handle parent, Mel_Button_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    HWND par = mel_gui__win32_parent_hwnd(n);
    if (!par) return h;

    wchar_t wbuf[1024];
    mel_gui__win32_widen(o.text, wbuf, 1024);

    HWND hwnd = CreateWindowExW(0, L"BUTTON", wbuf,
        mel_gui__win32_child_style(n, o.disabled) | WS_TABSTOP | BS_PUSHBUTTON,
        n->x, n->y, n->width, n->height, par, NULL, current_hinst, NULL);
    n->native = hwnd;
    if (!hwnd) return h;

    Mel_Win32_Button* b = (Mel_Win32_Button*)mel_gui__win32_alloc_ctl(hwnd, sizeof *b, h);
    if (b) {
        b->base.focus    = o.focus;
        b->base.keyboard = o.keyboard;
        b->pointer       = o.pointer;
    }
    SetWindowSubclass(hwnd, button_subclass, 1, 0);
    return h;
}
