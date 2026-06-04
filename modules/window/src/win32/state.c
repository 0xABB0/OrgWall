#include "../window_internal.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>

static HWND win32_hwnd(Mel_Window_Node* n) { return n && n->native ? (HWND)n->native : NULL; }

static bool win32_set_min_size(Mel_Window_Node* n, i32 w, i32 h)
{
    (void)w;
    (void)h;
    return win32_hwnd(n) != NULL;
}

static bool win32_set_max_size(Mel_Window_Node* n, i32 w, i32 h)
{
    (void)w;
    (void)h;
    return win32_hwnd(n) != NULL;
}

static bool win32_set_aspect(Mel_Window_Node* n, f32 min_ratio, f32 max_ratio)
{
    (void)min_ratio;
    (void)max_ratio;
    return win32_hwnd(n) != NULL;
}

static bool win32_set_fullscreen(Mel_Window_Node* n, u32 flags)
{
    HWND hwnd = win32_hwnd(n);
    if (!hwnd)
        return false;
    if (flags == MEL_WINDOW_FULLSCREEN_OFF)
    {
        SetWindowLongPtrW(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowPos(hwnd, NULL, n->x, n->y, n->w, n->h, SWP_FRAMECHANGED | SWP_NOZORDER);
        return true;
    }
    MONITORINFO mi = { .cbSize = sizeof(mi) };
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi))
        return false;
    SetWindowLongPtrW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top, SWP_FRAMECHANGED);
    return true;
}

static bool win32_set_fullscreen_mode(Mel_Window_Node* n, Mel_Window_Video_Mode mode)
{
    (void)mode;
    return win32_hwnd(n) != NULL;
}

static bool win32_get_fullscreen_mode(Mel_Window_Node* n, Mel_Window_Video_Mode* out)
{
    HWND hwnd = win32_hwnd(n);
    if (!hwnd)
        return false;
    MONITORINFOEXW mi = { .cbSize = sizeof(mi) };
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), (MONITORINFO*)&mi))
        return false;
    DEVMODEW dm = { .dmSize = sizeof(dm) };
    if (!EnumDisplaySettingsW(mi.szDevice, ENUM_CURRENT_SETTINGS, &dm))
        return false;
    out->width_px = dm.dmPelsWidth;
    out->height_px = dm.dmPelsHeight;
    out->refresh_mhz = dm.dmDisplayFrequency * 1000u;
    out->format_flags = MEL_WINDOW_PIXEL_BGRA8 | MEL_WINDOW_PIXEL_SRGB;
    return true;
}

static bool win32_set_opacity(Mel_Window_Node* n, f32 opacity)
{
    HWND hwnd = win32_hwnd(n);
    if (!hwnd)
        return false;
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
    return SetLayeredWindowAttributes(hwnd, 0, (BYTE)(opacity * 255.0f + 0.5f), LWA_ALPHA) != 0;
}

static bool win32_set_always_on_top(Mel_Window_Node* n, bool on)
{
    HWND hwnd = win32_hwnd(n);
    if (!hwnd)
        return false;
    return SetWindowPos(hwnd, on ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE) != 0;
}

static bool win32_set_borderless(Mel_Window_Node* n, bool borderless)
{
    HWND hwnd = win32_hwnd(n);
    if (!hwnd)
        return false;
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if (borderless)
        style = (style & ~(LONG_PTR)WS_OVERLAPPEDWINDOW) | WS_POPUP;
    else
        style = (style & ~(LONG_PTR)WS_POPUP) | WS_OVERLAPPEDWINDOW;
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    return true;
}

static bool win32_set_resizable(Mel_Window_Node* n, bool resizable)
{
    HWND hwnd = win32_hwnd(n);
    if (!hwnd)
        return false;
    LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if (resizable)
        style |= (WS_THICKFRAME | WS_MAXIMIZEBOX);
    else
        style &= ~(LONG_PTR)(WS_THICKFRAME | WS_MAXIMIZEBOX);
    SetWindowLongPtrW(hwnd, GWL_STYLE, style);
    SetWindowPos(hwnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
    return true;
}

static bool win32_set_icon(Mel_Window_Node* n, const u8* rgba, i32 w, i32 h)
{
    HWND hwnd = win32_hwnd(n);
    if (!hwnd || !rgba)
        return false;
    HBITMAP color = CreateBitmap(w, h, 1, 32, rgba);
    HBITMAP mask = CreateBitmap(w, h, 1, 1, NULL);
    ICONINFO ii = { .fIcon = TRUE, .hbmMask = mask, .hbmColor = color };
    HICON    icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    if (!icon)
        return false;
    SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)icon);
    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)icon);
    return true;
}

static bool win32_set_modal(Mel_Window_Node* n, bool modal)
{
    HWND hwnd = win32_hwnd(n);
    if (!hwnd)
        return false;
    EnableWindow(GetWindow(hwnd, GW_OWNER), modal ? FALSE : TRUE);
    return true;
}

static bool win32_set_parent(Mel_Window_Node* n, Mel_Window_Node* parent)
{
    HWND hwnd = win32_hwnd(n);
    if (!hwnd)
        return false;
    SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, parent && parent->native ? (LONG_PTR)parent->native : 0);
    return true;
}

static bool win32_set_shape(Mel_Window_Node* n, const u8* alpha, i32 w, i32 h)
{
    (void)alpha;
    (void)w;
    (void)h;
    return win32_hwnd(n) != NULL;
}

static bool win32_set_mouse_grab(Mel_Window_Node* n, bool grab)
{
    HWND hwnd = win32_hwnd(n);
    if (!hwnd)
        return false;
    if (grab)
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        POINT tl = { rc.left, rc.top };
        POINT br = { rc.right, rc.bottom };
        ClientToScreen(hwnd, &tl);
        ClientToScreen(hwnd, &br);
        RECT clip = { tl.x, tl.y, br.x, br.y };
        return ClipCursor(&clip) != 0;
    }
    return ClipCursor(NULL) != 0;
}

static bool win32_set_keyboard_grab(Mel_Window_Node* n, bool grab)
{
    (void)grab;
    return win32_hwnd(n) != NULL;
}

static bool win32_set_mouse_rect(Mel_Window_Node* n, Mel_Window_Rect rect)
{
    HWND hwnd = win32_hwnd(n);
    if (!hwnd)
        return false;
    if (rect.w <= 0 || rect.h <= 0)
        return ClipCursor(NULL) != 0;
    POINT tl = { rect.x, rect.y };
    POINT br = { rect.x + rect.w, rect.y + rect.h };
    ClientToScreen(hwnd, &tl);
    ClientToScreen(hwnd, &br);
    RECT clip = { tl.x, tl.y, br.x, br.y };
    return ClipCursor(&clip) != 0;
}

static ITaskbarList3* win32_taskbar(void)
{
    static ITaskbarList3* g_tbl;
    static bool           g_tried;
    if (!g_tried)
    {
        g_tried = true;
        CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        const GUID clsid = { 0x56FDF344, 0xFD6D, 0x11d0, { 0x95, 0x8A, 0x00, 0x60, 0x97, 0xC9, 0xA0, 0x90 } };
        const GUID iid = { 0xea1afb91, 0x9e28, 0x4b86, { 0x90, 0xE9, 0x9e, 0x9f, 0x8a, 0x5e, 0xef, 0xaf } };
        CoCreateInstance(&clsid, NULL, CLSCTX_INPROC_SERVER, &iid, (void**)&g_tbl);
    }
    return g_tbl;
}

static bool win32_set_progress_state(Mel_Window_Node* n, u32 state)
{
    HWND hwnd = win32_hwnd(n);
    ITaskbarList3* tbl = win32_taskbar();
    if (!hwnd || !tbl)
        return false;
    TBPFLAG f = TBPF_NOPROGRESS;
    if (state & MEL_WINDOW_PROGRESS_INDETERMINATE)
        f = TBPF_INDETERMINATE;
    else if (state & MEL_WINDOW_PROGRESS_ERROR)
        f = TBPF_ERROR;
    else if (state & MEL_WINDOW_PROGRESS_PAUSED)
        f = TBPF_PAUSED;
    else if (state & MEL_WINDOW_PROGRESS_NORMAL)
        f = TBPF_NORMAL;
    tbl->lpVtbl->SetProgressState(tbl, hwnd, f);
    return true;
}

static bool win32_set_progress_value(Mel_Window_Node* n, f32 value)
{
    HWND hwnd = win32_hwnd(n);
    ITaskbarList3* tbl = win32_taskbar();
    if (!hwnd || !tbl)
        return false;
    tbl->lpVtbl->SetProgressValue(tbl, hwnd, (ULONGLONG)(value * 1000.0f + 0.5f), 1000);
    return true;
}

static bool win32_safe_area(Mel_Window_Node* n, Mel_Window_Rect* out)
{
    HWND hwnd = win32_hwnd(n);
    if (!hwnd)
        return false;
    RECT rc;
    GetClientRect(hwnd, &rc);
    out->x = 0;
    out->y = 0;
    out->w = rc.right - rc.left;
    out->h = rc.bottom - rc.top;
    return true;
}

static bool win32_pixel_format(Mel_Window_Node* n, u32* out_flags)
{
    (void)n;
    *out_flags = MEL_WINDOW_PIXEL_BGRA8 | MEL_WINDOW_PIXEL_SRGB;
    return true;
}

static u64 win32_native_id(Mel_Window_Node* n)
{
    HWND hwnd = win32_hwnd(n);
    return hwnd ? (u64)(uintptr_t)hwnd : 0;
}

static bool win32_maximize(Mel_Window_Node* n)
{
    HWND hwnd = win32_hwnd(n);
    if (!hwnd)
        return false;
    ShowWindow(hwnd, SW_MAXIMIZE);
    return true;
}

static bool win32_minimize(Mel_Window_Node* n)
{
    HWND hwnd = win32_hwnd(n);
    if (!hwnd)
        return false;
    ShowWindow(hwnd, SW_MINIMIZE);
    return true;
}

static bool win32_restore(Mel_Window_Node* n)
{
    HWND hwnd = win32_hwnd(n);
    if (!hwnd)
        return false;
    ShowWindow(hwnd, SW_RESTORE);
    return true;
}

static bool win32_raise(Mel_Window_Node* n)
{
    HWND hwnd = win32_hwnd(n);
    if (!hwnd)
        return false;
    SetWindowPos(hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    return true;
}

static bool win32_flash(Mel_Window_Node* n, u32 flags)
{
    HWND hwnd = win32_hwnd(n);
    if (!hwnd)
        return false;
    FLASHWINFO fw = { .cbSize = sizeof(fw), .hwnd = hwnd };
    if (flags == MEL_WINDOW_FLASH_CANCEL)
        fw.dwFlags = FLASHW_STOP;
    else if (flags & MEL_WINDOW_FLASH_UNTIL_FOCUS)
        fw.dwFlags = FLASHW_ALL | FLASHW_TIMERNOFG;
    else
        fw.dwFlags = FLASHW_ALL;
    FlashWindowEx(&fw);
    return true;
}

static bool win32_get_surface(Mel_Window_Node* n, Mel_Window_Surface* out)
{
    HWND hwnd = win32_hwnd(n);
    if (!hwnd)
        return false;
    i32 stride = n->w * 4;
    if (!n->surface_pixels || n->surface_w != n->w || n->surface_h != n->h)
    {
        const Mel_Alloc* a = mel_window__alloc();
        if (n->surface_pixels)
            mel_dealloc(a, n->surface_pixels);
        n->surface_pixels = mel_alloc(a, (usize)stride * (usize)(n->h > 0 ? n->h : 1));
        n->surface_w = n->w;
        n->surface_h = n->h;
        n->surface_stride = stride;
        n->surface_format = MEL_WINDOW_PIXEL_BGRA8 | MEL_WINDOW_PIXEL_SRGB;
    }
    out->pixels = n->surface_pixels;
    out->width_px = n->surface_w;
    out->height_px = n->surface_h;
    out->stride_bytes = n->surface_stride;
    out->format_flags = n->surface_format;
    return true;
}

static bool win32_present_surface(Mel_Window_Node* n)
{
    HWND hwnd = win32_hwnd(n);
    if (!hwnd || !n->surface_pixels)
        return false;
    HDC dc = GetDC(hwnd);
    if (!dc)
        return false;
    BITMAPINFO bi = { 0 };
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = n->surface_w;
    bi.bmiHeader.biHeight = -n->surface_h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    SetDIBitsToDevice(dc, 0, 0, (DWORD)n->surface_w, (DWORD)n->surface_h, 0, 0, 0, (UINT)n->surface_h, n->surface_pixels, &bi, DIB_RGB_COLORS);
    ReleaseDC(hwnd, dc);
    return true;
}

static bool win32_icc_profile(Mel_Window_Node* n, Mel_Window_Icc_Profile* out)
{
    HWND hwnd = win32_hwnd(n);
    if (!hwnd)
        return false;
    HDC  dc = GetDC(hwnd);
    if (!dc)
        return false;
    WCHAR path[MAX_PATH];
    DWORD len = MAX_PATH;
    BOOL  ok = GetICMProfileW(dc, &len, path);
    ReleaseDC(hwnd, dc);
    if (!ok)
        return false;
    HANDLE f = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    DWORD size = GetFileSize(f, NULL);
    if (size == 0 || size == INVALID_FILE_SIZE)
    {
        CloseHandle(f);
        return false;
    }
    const Mel_Alloc* a = mel_window__alloc();
    u8*   buf = (u8*)mel_alloc(a, size);
    DWORD read = 0;
    BOOL  rok = ReadFile(f, buf, size, &read, NULL);
    CloseHandle(f);
    if (!rok || read != size)
    {
        mel_dealloc(a, buf);
        return false;
    }
    out->data = buf;
    out->size = size;
    return true;
}

static u32 win32_live_flags(Mel_Window_Node* n)
{
    HWND hwnd = win32_hwnd(n);
    if (!hwnd)
        return 0;
    u32 flags = 0;
    if (IsWindowVisible(hwnd))
        flags |= MEL_WINDOW_STATE_SHOWN;
    else
        flags |= MEL_WINDOW_STATE_HIDDEN;
    if (IsIconic(hwnd))
        flags |= MEL_WINDOW_STATE_MINIMIZED;
    if (IsZoomed(hwnd))
        flags |= MEL_WINDOW_STATE_MAXIMIZED;
    if (GetForegroundWindow() == hwnd)
        flags |= MEL_WINDOW_STATE_FOCUSED;
    return flags;
}

static const Mel_Window_Backend_Ops g_win32_ops = {
    .set_min_size = win32_set_min_size,
    .set_max_size = win32_set_max_size,
    .set_aspect = win32_set_aspect,
    .set_fullscreen = win32_set_fullscreen,
    .set_fullscreen_mode = win32_set_fullscreen_mode,
    .get_fullscreen_mode = win32_get_fullscreen_mode,
    .set_opacity = win32_set_opacity,
    .set_always_on_top = win32_set_always_on_top,
    .set_borderless = win32_set_borderless,
    .set_resizable = win32_set_resizable,
    .set_icon = win32_set_icon,
    .set_modal = win32_set_modal,
    .set_parent = win32_set_parent,
    .set_shape = win32_set_shape,
    .set_mouse_grab = win32_set_mouse_grab,
    .set_keyboard_grab = win32_set_keyboard_grab,
    .set_mouse_rect = win32_set_mouse_rect,
    .set_progress_state = win32_set_progress_state,
    .set_progress_value = win32_set_progress_value,
    .safe_area = win32_safe_area,
    .pixel_format = win32_pixel_format,
    .native_id = win32_native_id,
    .maximize = win32_maximize,
    .minimize = win32_minimize,
    .restore = win32_restore,
    .raise = win32_raise,
    .flash = win32_flash,
    .get_surface = win32_get_surface,
    .present_surface = win32_present_surface,
    .icc_profile = win32_icc_profile,
    .live_flags = win32_live_flags,
};

const Mel_Window_Backend_Ops* mel_window__backend_ops(void) { return &g_win32_ops; }
