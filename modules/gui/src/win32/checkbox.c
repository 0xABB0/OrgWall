#include "win32.h"

static LRESULT CALLBACK checkbox_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                          UINT_PTR id, DWORD_PTR ref)
{
    (void)ref;
    if (msg == MEL_REFLECT(WM_COMMAND)) {
        if (HIWORD(wp) == BN_CLICKED) {
            Mel_Win32_CheckBox* cb = (Mel_Win32_CheckBox*)mel_gui__win32_ctl(hwnd);
            if (cb && cb->on_.on_toggled) {
                bool checked = SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
                cb->on_.on_toggled(cb->base.handle, checked, mel_gui_user(cb->base.handle));
            }
        }
        return 0;
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, checkbox_subclass, id);
        mel_gui__win32_free_ctl(hwnd);
        return DefSubclassProc(hwnd, msg, wp, lp);
    }
    if (mel_gui__win32_subclass_common(hwnd, msg, wp, lp)) return 0;
    return DefSubclassProc(hwnd, msg, wp, lp);
}

Mel_Gui_Handle mel_checkbox_create_opt(Mel_Gui_Handle parent, Mel_CheckBox_Opt o)
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
        mel_gui__win32_child_style(n, o.disabled) | WS_TABSTOP | BS_AUTOCHECKBOX,
        n->x, n->y, n->width, n->height, par, NULL, current_hinst, NULL);
    n->native = hwnd;
    if (!hwnd) return h;

    Mel_Win32_CheckBox* cb = (Mel_Win32_CheckBox*)mel_gui__win32_alloc_ctl(hwnd, sizeof *cb, h);
    if (cb) {
        cb->base.focus    = o.focus;
        cb->base.keyboard = o.keyboard;
        cb->on_           = o.on_;
    }
    SendMessageW(hwnd, BM_SETCHECK, o.checked ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowSubclass(hwnd, checkbox_subclass, 1, 0);
    return h;
}

bool mel_checkbox_checked(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native) return false;
    return SendMessageW((HWND)n->native, BM_GETCHECK, 0, 0) == BST_CHECKED;
}
