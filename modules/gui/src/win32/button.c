#include "win32.h"

static LRESULT CALLBACK button_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                        UINT_PTR id, DWORD_PTR ref)
{
    (void)ref;
    Mel_Gui_Handle h = mel_gui__win32_handle_of(hwnd);

    if (msg == MEL_REFLECT(WM_COMMAND)) {
        if (HIWORD(wp) == BN_CLICKED) mel_gui__fire_click(h);
        return 0;
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, button_subclass, id);
    if (mel_gui__win32_subclass_common(hwnd, msg, wp, lp, h)) return 0;
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void mel_gui__backend_button_create(Mel_Gui_Widget* w, str8 text)
{
    HWND parent = mel_gui__win32_parent_hwnd(w);
    if (!parent) return;

    wchar_t wbuf[1024];
    mel_gui__win32_widen(text, wbuf, 1024);

    HWND hwnd = CreateWindowExW(0, L"BUTTON", wbuf,
        mel_gui__win32_child_style(w) | WS_TABSTOP | BS_PUSHBUTTON,
        w->x, w->y, w->width, w->height, parent, NULL, current_hinst, NULL);

    w->native = hwnd;
    if (!hwnd) return;

    mel_gui__win32_bind(hwnd, w->self);
    SetWindowSubclass(hwnd, button_subclass, 1, 0);
}
