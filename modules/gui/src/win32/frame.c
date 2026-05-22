#include "win32.h"

static const wchar_t* FRAME_CLASS = L"MelGuiFrame";
static bool           g_frame_class;

static LRESULT CALLBACK frame_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_COMMAND:
            if (lp) SendMessageW((HWND)lp, MEL_REFLECT(WM_COMMAND), wp, lp);
            return 0;
        case WM_HSCROLL:
            if (lp) SendMessageW((HWND)lp, MEL_REFLECT(WM_HSCROLL), wp, lp);
            return 0;
        case WM_CTLCOLORSTATIC:
            SetBkMode((HDC)wp, TRANSPARENT);
            return (LRESULT)(UINT_PTR)GetSysColorBrush(COLOR_BTNFACE);
        case WM_SIZE:
            mel_gui__fire_resize(mel_gui__win32_handle_of(hwnd),
                                 (i32)LOWORD(lp), (i32)HIWORD(lp));
            return 0;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY: {
            Mel_Gui_Handle h = mel_gui__win32_handle_of(hwnd);
            if (!mel_gui_handle_is_none(h)) mel_gui__destroy_tree(h);
            if (mel_gui__frames_dec() == 0) PostQuitMessage(0);
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void ensure_frame_class(void)
{
    if (g_frame_class) return;
    WNDCLASSEXW fc = {0};
    fc.cbSize        = sizeof fc;
    fc.style         = CS_HREDRAW | CS_VREDRAW;
    fc.lpfnWndProc   = frame_wndproc;
    fc.hInstance     = current_hinst;
    fc.hCursor       = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    fc.hbrBackground = (HBRUSH)(UINT_PTR)(COLOR_BTNFACE + 1);
    fc.lpszClassName = FRAME_CLASS;
    RegisterClassExW(&fc);
    g_frame_class = true;
}

void mel_gui__backend_frame_create(Mel_Gui_Widget* w, str8 title)
{
    ensure_frame_class();

    wchar_t wbuf[1024];
    mel_gui__win32_widen(title, wbuf, 1024);

    i32  cw = w->width  > 0 ? w->width  : 480;
    i32  ch = w->height > 0 ? w->height : 360;
    RECT rc = { 0, 0, cw, ch };
    AdjustWindowRectEx(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0);

    int wx = (w->x != 0) ? w->x : CW_USEDEFAULT;
    int wy = (w->y != 0) ? w->y : CW_USEDEFAULT;

    HWND hwnd = CreateWindowExW(0, FRAME_CLASS, wbuf, WS_OVERLAPPEDWINDOW,
        wx, wy, rc.right - rc.left, rc.bottom - rc.top,
        NULL, NULL, current_hinst, NULL);

    w->native = hwnd;
    if (!hwnd) return;

    mel_gui__win32_bind(hwnd, w->self);

    RECT got;
    GetWindowRect(hwnd, &got);
    w->x = got.left;
    w->y = got.top;

    mel_gui__frames_inc();
}
