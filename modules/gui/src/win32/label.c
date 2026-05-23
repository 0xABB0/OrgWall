#include "win32.h"

Mel_Gui_Handle mel_label_create_opt(Mel_Gui_Handle parent, Mel_Label_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    HWND par = mel_gui__win32_parent_hwnd(n);
    if (!par) return h;

    wchar_t wbuf[1024];
    mel_gui__win32_widen(o.text, wbuf, 1024);

    HWND hwnd = CreateWindowExW(0, L"STATIC", wbuf,
        mel_gui__win32_child_style(n, false) | SS_LEFT | SS_NOPREFIX,
        n->x, n->y, n->width, n->height, par, NULL, current_hinst, NULL);
    n->native = hwnd;
    return h;
}
