#include "win32.h"

static LRESULT CALLBACK checkbox_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                          UINT_PTR id, DWORD_PTR ref)
{
    (void)ref;
    Mel_Gui_Handle h = mel_gui__win32_handle_of(hwnd);

    if (msg == MEL_REFLECT(WM_COMMAND)) {
        if (HIWORD(wp) == BN_CLICKED) {
            bool            checked = SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
            Mel_Gui_Widget* w       = mel_gui__get(h);
            Mel_Gui_CheckBox_Impl* impl = w ? (Mel_Gui_CheckBox_Impl*)w->impl : NULL;
            if (impl && impl->on_.on_toggled) {
                impl->on_.on_toggled(h, checked, w->user);
            }
        }
        return 0;
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, checkbox_subclass, id);
    if (mel_gui__win32_subclass_common(hwnd, msg, wp, lp, h)) return 0;
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void mel_gui__backend_checkbox_create(Mel_Gui_Widget* w, str8 text)
{
    HWND parent = mel_gui__win32_parent_hwnd(w);
    if (!parent) return;

    wchar_t wbuf[1024];
    mel_gui__win32_widen(text, wbuf, 1024);

    HWND hwnd = CreateWindowExW(0, L"BUTTON", wbuf,
        mel_gui__win32_child_style(w) | WS_TABSTOP | BS_AUTOCHECKBOX,
        w->x, w->y, w->width, w->height, parent, NULL, current_hinst, NULL);

    w->native = hwnd;
    if (!hwnd) return;

    mel_gui__win32_bind(hwnd, w->self);

    Mel_Gui_CheckBox_Impl* impl = (Mel_Gui_CheckBox_Impl*)w->impl;
    if (impl) {
        SendMessageW(hwnd, BM_SETCHECK,
            impl->initial_checked ? BST_CHECKED : BST_UNCHECKED, 0);
    }

    SetWindowSubclass(hwnd, checkbox_subclass, 1, 0);
}

bool mel_gui__backend_checkbox_checked(Mel_Gui_Widget* w)
{
    if (!w || !w->native) return false;
    return SendMessageW((HWND)w->native, BM_GETCHECK, 0, 0) == BST_CHECKED;
}
