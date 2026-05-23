#include "win32.h"

static LRESULT CALLBACK textfield_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                           UINT_PTR id, DWORD_PTR ref)
{
    (void)ref;
    if (msg == MEL_REFLECT(WM_COMMAND)) {
        if (HIWORD(wp) == EN_CHANGE) {
            Mel_Win32_TextField* tf = (Mel_Win32_TextField*)mel_gui__win32_ctl(hwnd);
            if (tf && tf->on_.on_text_changed) {
                wchar_t wbuf[2048];
                int     wn = GetWindowTextW(hwnd, wbuf, 2048);
                char    buf[2048];
                size    n  = mel_gui__win32_narrow(wbuf, wn, buf, sizeof buf);
                str8    text = { (u8*)buf, n };
                tf->on_.on_text_changed(tf->base.handle, text, mel_gui_user(tf->base.handle));
            }
        }
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, textfield_subclass, id);
        mel_gui__win32_free_ctl(hwnd);
        return DefSubclassProc(hwnd, msg, wp, lp);
    }
    if (mel_gui__win32_subclass_common(hwnd, msg, wp, lp)) return 0;
    return DefSubclassProc(hwnd, msg, wp, lp);
}

Mel_Gui_Handle mel_textfield_create_opt(Mel_Gui_Handle parent, Mel_TextField_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    HWND par = mel_gui__win32_parent_hwnd(n);
    if (!par) return h;

    wchar_t wbuf[1024];
    mel_gui__win32_widen(o.text, wbuf, 1024);

    HWND hwnd = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", wbuf,
        mel_gui__win32_child_style(n, o.disabled) | WS_TABSTOP | ES_AUTOHSCROLL | ES_LEFT,
        n->x, n->y, n->width, n->height, par, NULL, current_hinst, NULL);
    n->native = hwnd;
    if (!hwnd) return h;

    Mel_Win32_TextField* tf = (Mel_Win32_TextField*)mel_gui__win32_alloc_ctl(hwnd, sizeof *tf, h);
    if (tf) {
        tf->base.focus    = o.focus;
        tf->base.keyboard = o.keyboard;
        tf->on_           = o.on_;
    }
    SetWindowSubclass(hwnd, textfield_subclass, 1, 0);
    return h;
}
