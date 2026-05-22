#include "win32.h"

static LRESULT CALLBACK textfield_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                           UINT_PTR id, DWORD_PTR ref)
{
    (void)ref;
    Mel_Gui_Handle h = mel_gui__win32_handle_of(hwnd);

    if (msg == MEL_REFLECT(WM_COMMAND)) {
        if (HIWORD(wp) == EN_CHANGE) {
            Mel_Gui_Widget* w = mel_gui__get(h);
            Mel_Gui_TextField_Impl* impl = w ? (Mel_Gui_TextField_Impl*)w->impl : NULL;
            if (impl && impl->on_.on_text_changed) {
                char buf[1024];
                size n = mel_gui__backend_get_text(w, buf, sizeof buf);
                str8 text = { (u8*)buf, n };
                impl->on_.on_text_changed(h, text, w->user);
            }
        }
        return 0;
    }
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd, textfield_subclass, id);
    if (mel_gui__win32_subclass_common(hwnd, msg, wp, lp, h)) return 0;
    return DefSubclassProc(hwnd, msg, wp, lp);
}

void mel_gui__backend_textfield_create(Mel_Gui_Widget* w, str8 text)
{
    HWND parent = mel_gui__win32_parent_hwnd(w);
    if (!parent) return;

    wchar_t wbuf[1024];
    mel_gui__win32_widen(text, wbuf, 1024);

    HWND hwnd = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", wbuf,
        mel_gui__win32_child_style(w) | WS_TABSTOP | ES_AUTOHSCROLL | ES_LEFT,
        w->x, w->y, w->width, w->height, parent, NULL, current_hinst, NULL);

    w->native = hwnd;
    if (!hwnd) return;

    mel_gui__win32_bind(hwnd, w->self);
    SetWindowSubclass(hwnd, textfield_subclass, 1, 0);
}
