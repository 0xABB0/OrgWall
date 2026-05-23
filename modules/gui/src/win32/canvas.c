#include "win32.h"

static const wchar_t* CANVAS_CLASS = L"MelGuiCanvas";
static bool           g_canvas_class;

static LRESULT CALLBACK canvas_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Mel_Win32_Canvas* c = (Mel_Win32_Canvas*)mel_gui__win32_ctl(hwnd);
    Mel_Gui_Handle    h = c ? c->base.handle : MEL_GUI_HANDLE_NONE;
    void*             u = c ? mel_gui_user(h) : NULL;

    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC  dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            if (c && c->on_.on_paint) {
                struct Mel_Painter p = { .dc = dc, .w = rc.right - rc.left, .h = rc.bottom - rc.top };
                c->on_.on_paint(h, &p, rc.right - rc.left, rc.bottom - rc.top, u);
            } else {
                FillRect(dc, &rc, GetSysColorBrush(COLOR_WINDOW));
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_LBUTTONDOWN:
            SetFocus(hwnd);
            SetCapture(hwnd);
            if (c && c->pointer.on_pointer_down) c->pointer.on_pointer_down(h, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), u);
            return 0;
        case WM_MOUSEMOVE:
            if (c && c->pointer.on_pointer_move) c->pointer.on_pointer_move(h, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), u);
            return 0;
        case WM_LBUTTONUP:
            ReleaseCapture();
            if (c && c->pointer.on_pointer_up) c->pointer.on_pointer_up(h, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), u);
            return 0;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_WANTCHARS;
        case WM_NCDESTROY:
            mel_gui__win32_free_ctl(hwnd);
            break;
        default:
            break;
    }
    if (mel_gui__win32_subclass_common(hwnd, msg, wp, lp)) return 0;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void ensure_canvas_class(void)
{
    if (g_canvas_class) return;
    WNDCLASSEXW cc = {0};
    cc.cbSize        = sizeof cc;
    cc.style         = CS_HREDRAW | CS_VREDRAW;
    cc.lpfnWndProc   = canvas_wndproc;
    cc.hInstance     = current_hinst;
    cc.hCursor       = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    cc.hbrBackground = NULL;
    cc.lpszClassName = CANVAS_CLASS;
    RegisterClassExW(&cc);
    g_canvas_class = true;
}

Mel_Gui_Handle mel_canvas_create_opt(Mel_Gui_Handle parent, Mel_Canvas_Opt o)
{
    ensure_canvas_class();

    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    HWND par = mel_gui__win32_parent_hwnd(n);
    if (!par) return h;

    HWND hwnd = CreateWindowExW(0, CANVAS_CLASS, NULL,
        mel_gui__win32_child_style(n, false) | WS_TABSTOP,
        n->x, n->y, n->width, n->height, par, NULL, current_hinst, NULL);
    n->native = hwnd;
    if (!hwnd) return h;

    Mel_Win32_Canvas* c = (Mel_Win32_Canvas*)mel_gui__win32_alloc_ctl(hwnd, sizeof *c, h);
    if (c) {
        c->base.focus    = o.focus;
        c->base.keyboard = o.keyboard;
        c->pointer       = o.pointer;
        c->on_           = o.on_;
    }
    return h;
}
