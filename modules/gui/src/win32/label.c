#include "win32.h"

void mel_gui__backend_label_create(Mel_Gui_Widget* w, str8 text)
{
    HWND parent = mel_gui__win32_parent_hwnd(w);
    if (!parent) return;

    wchar_t wbuf[1024];
    mel_gui__win32_widen(text, wbuf, 1024);

    HWND hwnd = CreateWindowExW(0, L"STATIC", wbuf,
        mel_gui__win32_child_style(w) | SS_LEFT | SS_NOPREFIX,
        w->x, w->y, w->width, w->height, parent, NULL, current_hinst, NULL);

    w->native = hwnd;
    if (hwnd) mel_gui__win32_bind(hwnd, w->self);
}
