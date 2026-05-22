#include "win32.h"

static const wchar_t* CANVAS_CLASS = L"MelGuiCanvas";
static bool           g_canvas_class;

static LRESULT CALLBACK canvas_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Mel_Gui_Handle h = mel_gui__win32_handle_of(hwnd);

    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC  dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);
            Mel_Gui_Widget*      w    = mel_gui__get(h);
            Mel_Gui_Canvas_Impl* impl = w ? (Mel_Gui_Canvas_Impl*)w->impl : NULL;
            if (impl && impl->on_.on_paint) {
                impl->on_.on_paint(h, dc, rc.right - rc.left, rc.bottom - rc.top, w->user);
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
            mel_gui__fire_pointer_down(h, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
        case WM_MOUSEMOVE:
            mel_gui__fire_pointer_move(h, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
        case WM_LBUTTONUP:
            ReleaseCapture();
            mel_gui__fire_pointer_up(h, GET_X_LPARAM(lp), GET_Y_LPARAM(lp));
            return 0;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_WANTCHARS;
        default:
            break;
    }
    if (mel_gui__win32_subclass_common(hwnd, msg, wp, lp, h)) return 0;
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

void mel_gui__backend_canvas_create(Mel_Gui_Widget* w, str8 text)
{
    (void)text;
    ensure_canvas_class();

    HWND parent = mel_gui__win32_parent_hwnd(w);
    if (!parent) return;

    HWND hwnd = CreateWindowExW(0, CANVAS_CLASS, NULL,
        mel_gui__win32_child_style(w) | WS_TABSTOP,
        w->x, w->y, w->width, w->height, parent, NULL, current_hinst, NULL);

    w->native = hwnd;
    if (hwnd) mel_gui__win32_bind(hwnd, w->self);
}
