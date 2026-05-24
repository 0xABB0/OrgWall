#include "win32.h"

#define GROUPBOX_INSET_X 6
#define GROUPBOX_INSET_TOP 18
#define GROUPBOX_INSET_BOTTOM 8

static void inner_bounds(HWND box, RECT* out)
{
    RECT rc;
    GetClientRect(box, &rc);
    out->left   = GROUPBOX_INSET_X;
    out->top    = GROUPBOX_INSET_TOP;
    out->right  = (rc.right - rc.left) - GROUPBOX_INSET_X * 2;
    out->bottom = (rc.bottom - rc.top) - GROUPBOX_INSET_TOP - GROUPBOX_INSET_BOTTOM;
    if (out->right < 0)  out->right = 0;
    if (out->bottom < 0) out->bottom = 0;
}

static LRESULT CALLBACK groupbox_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                          UINT_PTR id, DWORD_PTR ref)
{
    HWND inner = (HWND)ref;
    if (msg == WM_SIZE) {
        RECT ib;
        inner_bounds(hwnd, &ib);
        MoveWindow(inner, ib.left, ib.top, ib.right, ib.bottom, TRUE);
        return DefSubclassProc(hwnd, msg, wp, lp);
    }
    if (msg == WM_NCDESTROY) {
        RemoveWindowSubclass(hwnd, groupbox_subclass, id);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

Mel_Gui_Handle mel_groupbox_create_opt(Mel_Gui_Handle parent, Mel_GroupBox_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    HWND par = mel_gui__win32_parent_hwnd(n);
    if (!par) return h;

    wchar_t wbuf[1024];
    mel_gui__win32_widen(o.title, wbuf, 1024);

    HWND box = CreateWindowExW(0, L"BUTTON", wbuf,
        mel_gui__win32_child_style(n, o.disabled) | BS_GROUPBOX,
        n->x, n->y, n->width, n->height, par, NULL, current_hinst, NULL);
    n->native = box;
    if (!box) return h;

    RECT ib;
    inner_bounds(box, &ib);
    HWND inner = mel_gui__win32_make_container(box, ib.left, ib.top, ib.right, ib.bottom, h,
        (Mel_Gui_Pointer_Cb){0}, o.focus, (Mel_Gui_Keyboard_Cb){0}, false, false);
    n->content = inner;

    SetWindowSubclass(box, groupbox_subclass, 1, (DWORD_PTR)inner);
    return h;
}
