#include "win32.h"

#include <gui/controls/gpu_view.h>

// The win32 gpu-view: a child HWND a GPU swapchain renders into (gui/controls/gpu_view.h). It owns no GDI
// painting — the swapchain owns the pixels — so the class has a NULL background and WM_ERASEBKGND is swallowed
// to avoid flicker. mel_gpu_view_surface returns the HWND for mel_gpu_surface_create (the win32 surface path).
typedef struct
{
    Mel_Win32_Ctl      base;
    Mel_Gui_Pointer_Cb pointer;
    Mel_Gpu_View_On    on_;
} Mel_Win32_GpuView;

static const wchar_t* GPUVIEW_CLASS = L"MelGuiGpuView";
static bool           g_gpuview_class;

static LRESULT CALLBACK gpuview_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Mel_Win32_GpuView* v = (Mel_Win32_GpuView*)mel_gui__win32_ctl(hwnd);
    Mel_Gui_Handle     h = v ? v->base.handle : MEL_GUI_HANDLE_NONE;
    void*              u = v ? mel_gui_user(h) : NULL;

    switch (msg)
    {
    case WM_SIZE:
        if (v && v->on_.on_resize)
            v->on_.on_resize(h, (i32)LOWORD(lp), (i32)HIWORD(lp), u);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        SetCapture(hwnd);
        if (v && v->pointer.on_pointer_down)
            v->pointer.on_pointer_down(h, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), u);
        return 0;
    case WM_MOUSEMOVE:
        if (v && v->pointer.on_pointer_move)
            v->pointer.on_pointer_move(h, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), u);
        return 0;
    case WM_LBUTTONUP:
        ReleaseCapture();
        if (v && v->pointer.on_pointer_up)
            v->pointer.on_pointer_up(h, GET_X_LPARAM(lp), GET_Y_LPARAM(lp), u);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;
    case WM_NCDESTROY:
        mel_gui__win32_free_ctl(hwnd);
        break;
    default:
        break;
    }
    if (mel_gui__win32_subclass_common(hwnd, msg, wp, lp))
        return 0;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void ensure_gpuview_class(void)
{
    if (g_gpuview_class)
        return;
    WNDCLASSEXW cc = { 0 };
    cc.cbSize = sizeof cc;
    cc.style = CS_HREDRAW | CS_VREDRAW;
    cc.lpfnWndProc = gpuview_wndproc;
    cc.hInstance = current_hinst;
    cc.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    cc.hbrBackground = NULL;
    cc.lpszClassName = GPUVIEW_CLASS;
    RegisterClassExW(&cc);
    g_gpuview_class = true;
}

Mel_Gui_Handle mel_gpu_view_create_opt(Mel_Gui_Handle parent, Mel_Gpu_View_Opt o)
{
    ensure_gpuview_class();

    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden, &o.layoutable, NULL);
    Mel_Gui_Node*  n = mel_gui__node(h);
    if (!n)
        return h;

    HWND par = mel_gui__win32_parent_hwnd(n);
    if (!par)
        return h;

    HWND hwnd = CreateWindowExW(0, GPUVIEW_CLASS, NULL, mel_gui__win32_child_style(n, false) | WS_TABSTOP, n->x, n->y, n->width, n->height, par, NULL, current_hinst, NULL);
    n->native = hwnd;
    if (!hwnd)
        return h;

    Mel_Win32_GpuView* v = (Mel_Win32_GpuView*)mel_gui__win32_alloc_ctl(hwnd, sizeof *v, h);
    if (v)
    {
        v->base.focus = o.focus;
        v->base.keyboard = o.keyboard;
        v->pointer = o.pointer;
        v->on_ = o.on_;
    }
    return h;
}

void* mel_gpu_view_surface(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    return n ? n->native : NULL;
}
