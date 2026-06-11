#include "../../src/window_internal.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>

static HINSTANCE   g_hinst;
static const WCHAR g_class_name[] = L"MelWindow";

static u64 mel_window__pack(Mel_Window w) { return ((u64)w.generation << 32) | (u64)w.index; }

static Mel_Window mel_window__unpack(u64 v) { return (Mel_Window){ .index = (u32)v, .generation = (u32)(v >> 32) }; }

static Mel_Window mel_window__from_hwnd(HWND hwnd) { return mel_window__unpack((u64)GetWindowLongPtrW(hwnd, GWLP_USERDATA)); }

static WCHAR* mel_window__widen(str8 s)
{
    const Mel_Alloc* a = mel_window__alloc();
    if (s.len <= 0 || s.data == NULL)
    {
        WCHAR* z = (WCHAR*)mel_alloc(a, sizeof(WCHAR));
        z[0] = 0;
        return z;
    }
    int    need = MultiByteToWideChar(CP_UTF8, 0, (const char*)s.data, (int)s.len, NULL, 0);
    WCHAR* buf = (WCHAR*)mel_alloc(a, sizeof(WCHAR) * (usize)(need + 1));
    MultiByteToWideChar(CP_UTF8, 0, (const char*)s.data, (int)s.len, buf, need);
    buf[need] = 0;
    return buf;
}

static void mel_window__sync_size(HWND hwnd, Mel_Window w, Mel_Window_Node* n)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    i32  pw = rc.right - rc.left;
    i32  ph = rc.bottom - rc.top;
    UINT dpi = GetDpiForWindow(hwnd);
    if (dpi == 0)
        dpi = 96;
    n->scale = (f32)dpi / 96.0f;
    n->point_w = (i32)((i64)pw * 96 / dpi);
    n->point_h = (i32)((i64)ph * 96 / dpi);
    mel_window__resized(w, pw, ph);
}

bool mel_window_should_close(Mel_Window w)
{
    Mel_Window_Node* n = mel_window__node(w);
    if (n && n->lifecycle.on_close_request)
        return n->lifecycle.on_close_request(w, n->user);
    return true;
}

static LRESULT CALLBACK mel_window__wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)cs->lpCreateParams);
        return DefWindowProcW(hwnd, msg, wp, lp);
    }

    Mel_Window       w = mel_window__from_hwnd(hwnd);
    Mel_Window_Node* n = mel_window__node(w);
    if (!n)
        return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg)
    {
    case WM_SIZE:
        mel_window__sync_size(hwnd, w, n);
        return 0;

    case WM_DPICHANGED:
    {
        UINT dpi = HIWORD(wp);
        f32  s = (f32)dpi / 96.0f;
        n->scale = s;
        if (n->display.on_scale_changed)
            n->display.on_scale_changed(w, s, n->user);
        if (n->display.on_hdr_changed)
            n->display.on_hdr_changed(w, n->user);
        RECT* sug = (RECT*)lp;
        SetWindowPos(hwnd, NULL, sug->left, sug->top, sug->right - sug->left, sug->bottom - sug->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }

    case WM_MOVE:
    {
        i32 x = (i32)(i16)LOWORD(lp);
        i32 y = (i32)(i16)HIWORD(lp);
        n->x = x;
        n->y = y;
        if (n->lifecycle.on_move)
            n->lifecycle.on_move(w, x, y, n->user);
        return 0;
    }

    case WM_SETFOCUS:
        if (n->lifecycle.on_focus_in)
            n->lifecycle.on_focus_in(w, n->user);
        return 0;

    case WM_KILLFOCUS:
        if (n->lifecycle.on_focus_out)
            n->lifecycle.on_focus_out(w, n->user);
        return 0;

    case WM_ACTIVATEAPP:
        if (wp)
        {
            if (n->app.on_foreground)
                n->app.on_foreground(w, n->user);
        }
        else
        {
            if (n->app.on_background)
                n->app.on_background(w, n->user);
        }
        return 0;

    case WM_LBUTTONDOWN:
        if (n->input.on_pointer_down)
            n->input.on_pointer_down(w, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), n->user);
        return 0;

    case WM_LBUTTONUP:
        if (n->input.on_pointer_up)
            n->input.on_pointer_up(w, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), n->user);
        return 0;

    case WM_MOUSEMOVE:
        if (n->input.on_pointer_move)
            n->input.on_pointer_move(w, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), n->user);
        return 0;

    case WM_KEYDOWN:
        if (n->input.on_key_down)
            n->input.on_key_down(w, (u32)wp, n->user);
        return 0;

    case WM_KEYUP:
        if (n->input.on_key_up)
            n->input.on_key_up(w, (u32)wp, n->user);
        return 0;

    case WM_CLOSE:
        if (!mel_window_should_close(w))
            return 0;
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        n->native = NULL;
        n->content = NULL;
        mel_window__closed(w);
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

bool mel_window__backend_init(void)
{
    g_hinst = GetModuleHandleW(NULL);

    WNDCLASSEXW wc = { 0 };
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = mel_window__wndproc;
    wc.hInstance = g_hinst;
    wc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = g_class_name;
    RegisterClassExW(&wc);
    return true;
}

void mel_window__backend_create(Mel_Window_Node* n, const Mel_Window_Opt* o)
{
    DWORD style = WS_OVERLAPPEDWINDOW;
    if (o->undecorated)
    {
        style = WS_POPUP;
    }
    else if (o->not_resizable)
    {
        style &= ~(DWORD)(WS_THICKFRAME | WS_MAXIMIZEBOX);
    }

    RECT rc = { 0, 0, n->w, n->h };
    AdjustWindowRectEx(&rc, style, FALSE, 0);

    u64    packed = mel_window__pack(n->self);
    WCHAR* title = mel_window__widen(o->title);

    HWND hwnd = CreateWindowExW(0, g_class_name, title, style, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top, NULL, NULL, g_hinst, (LPVOID)(uintptr_t)packed);
    mel_dealloc(mel_window__alloc(), title);
    if (!hwnd)
        return;

    n->native = (void*)hwnd;
    n->content = (void*)hwnd;

    UINT dpi = GetDpiForWindow(hwnd);
    if (dpi == 0)
        dpi = 96;
    n->scale = (f32)dpi / 96.0f;
    n->point_w = n->w;
    n->point_h = n->h;

    if (!o->start_hidden)
        ShowWindow(hwnd, SW_SHOW);
}

void mel_window__backend_destroy(Mel_Window_Node* n)
{
    if (!n || !n->native)
        return;
    DestroyWindow((HWND)n->native);
}

void mel_window_set_title(Mel_Window w, str8 title)
{
    Mel_Window_Node* n = mel_window__node(w);
    if (!n || !n->native)
        return;
    WCHAR* t = mel_window__widen(title);
    SetWindowTextW((HWND)n->native, t);
    mel_dealloc(mel_window__alloc(), t);
}

void mel_window_set_bounds(Mel_Window w, i32 x, i32 y, i32 width, i32 height)
{
    Mel_Window_Node* n = mel_window__node(w);
    if (!n)
        return;
    n->x = x;
    n->y = y;
    n->w = width;
    n->h = height;
    if (!n->native)
        return;

    HWND  hwnd = (HWND)n->native;
    DWORD style = (DWORD)GetWindowLongPtrW(hwnd, GWL_STYLE);
    RECT  rc = { x, y, x + width, y + height };
    AdjustWindowRectEx(&rc, style, FALSE, 0);
    SetWindowPos(hwnd, NULL, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

void mel_window_set_visible(Mel_Window w, bool visible)
{
    Mel_Window_Node* n = mel_window__node(w);
    if (!n || !n->native)
        return;
    ShowWindow((HWND)n->native, visible ? SW_SHOW : SW_HIDE);
}

void mel_window_set_focus(Mel_Window w)
{
    Mel_Window_Node* n = mel_window__node(w);
    if (!n || !n->native)
        return;
    SetForegroundWindow((HWND)n->native);
    SetFocus((HWND)n->native);
}

void mel_window_refresh(Mel_Window w)
{
    Mel_Window_Node* n = mel_window__node(w);
    if (!n || !n->native)
        return;
    InvalidateRect((HWND)n->native, NULL, FALSE);
}
