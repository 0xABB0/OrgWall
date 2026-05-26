#include "win32.h"

static const wchar_t* CONTAINER_CLASS = L"MelGuiContainer";
static bool           g_container_class;

static LRESULT CALLBACK container_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Mel_Win32_Panel* p = (Mel_Win32_Panel*)mel_gui__win32_ctl(hwnd);
    Mel_Gui_Handle   h = p ? p->base.handle : MEL_GUI_HANDLE_NONE;
    void*            u = p ? mel_gui_user(h) : NULL;

    switch (msg) {
        case WM_COMMAND:
            if (lp) { SendMessageW((HWND)lp, MEL_REFLECT(WM_COMMAND), wp, lp); return 0; }
            break;
        case WM_HSCROLL:
            if (lp) { SendMessageW((HWND)lp, MEL_REFLECT(WM_HSCROLL), wp, lp); return 0; }
            break;
        case WM_CTLCOLORSTATIC:
            SetBkMode((HDC)wp, TRANSPARENT);
            return (LRESULT)(UINT_PTR)GetSysColorBrush(COLOR_BTNFACE);
        case WM_LBUTTONDOWN:
            if (p && p->pointer.on_pointer_down) p->pointer.on_pointer_down(h, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), u);
            return 0;
        case WM_MOUSEMOVE:
            if (p && p->pointer.on_pointer_move) p->pointer.on_pointer_move(h, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), u);
            return 0;
        case WM_LBUTTONUP:
            if (p && p->pointer.on_pointer_up) p->pointer.on_pointer_up(h, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), u);
            if (p && p->pointer.on_click)      p->pointer.on_click(h, u);
            return 0;
        case WM_NCDESTROY:
            mel_gui__win32_free_ctl(hwnd);
            break;
        default:
            break;
    }
    if (mel_gui__win32_subclass_common(hwnd, msg, wp, lp)) return 0;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void mel_gui__win32_ensure_container_class(void)
{
    if (g_container_class) return;
    WNDCLASSEXW cc = {0};
    cc.cbSize        = sizeof cc;
    cc.style         = CS_HREDRAW | CS_VREDRAW;
    cc.lpfnWndProc   = container_wndproc;
    cc.hInstance     = current_hinst;
    cc.hCursor       = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    cc.hbrBackground = (HBRUSH)(UINT_PTR)(COLOR_BTNFACE + 1);
    cc.lpszClassName = CONTAINER_CLASS;
    RegisterClassExW(&cc);
    g_container_class = true;
}

HWND mel_gui__win32_make_container(HWND parent, i32 x, i32 y, i32 w, i32 h,
                                   Mel_Gui_Handle handle, Mel_Gui_Pointer_Cb pointer,
                                   Mel_Gui_Focus_Cb focus, Mel_Gui_Keyboard_Cb keyboard,
                                   bool hidden, bool disabled)
{
    mel_gui__win32_ensure_container_class();

    DWORD style = WS_CHILD | WS_CLIPCHILDREN;
    if (!hidden)  style |= WS_VISIBLE;
    if (disabled) style |= WS_DISABLED;

    HWND hwnd = CreateWindowExW(0, CONTAINER_CLASS, NULL, style,
        x, y, w, h, parent, NULL, current_hinst, NULL);
    if (!hwnd) return NULL;

    Mel_Win32_Panel* p = (Mel_Win32_Panel*)mel_gui__win32_alloc_ctl(hwnd, sizeof *p, handle);
    if (p) {
        p->base.focus    = focus;
        p->base.keyboard = keyboard;
        p->pointer       = pointer;
    }
    return hwnd;
}

Mel_Gui_Handle mel_panel_create_opt(Mel_Gui_Handle parent, Mel_Panel_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    HWND par = mel_gui__win32_parent_hwnd(n);
    if (!par) return h;

    HWND hwnd = mel_gui__win32_make_container(par, n->x, n->y, n->width, n->height, h,
                                              o.pointer, o.focus, o.keyboard, n->hidden, o.disabled);
    n->native = hwnd;
    return h;
}
