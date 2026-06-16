#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <gdiplus.h>
#include <string>
#include <stdexec/execution.hpp>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "gdiplus.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif
#ifndef DWMSBT_MAINWINDOW
#define DWMSBT_MAINWINDOW 2
#endif
#ifndef DWMSBT_TRANSIENTWINDOW
#define DWMSBT_TRANSIENTWINDOW 4
#endif

namespace ctl {
    enum {
        ID_NAV = 100,
        ID_NAME_EDIT = 101,
        ID_ROLE_COMBO = 102,
        ID_NOTIFY = 103,
        ID_DARK = 104,
        ID_BACKDROP = 105,
        ID_VOLUME_TRACK = 106,
        ID_PROGRESS = 107,
        ID_START_BTN = 108,
        ID_RESET_BTN = 109,
        ID_LIST = 110,
        ID_STATUS = 111,
    };
}

static HFONT g_uiFont = nullptr;
static HFONT g_titleFont = nullptr;
static HBRUSH g_bgBrush = nullptr;
static COLORREF g_bg = RGB(32, 32, 32);
static COLORREF g_fg = RGB(240, 240, 240);
static COLORREF g_subFg = RGB(170, 170, 170);
static bool g_dark = true;
static bool g_acrylic = false;
static ULONG_PTR g_gdiToken = 0;

static HWND g_name, g_role, g_notify, g_darkChk, g_backdrop, g_volume, g_progress;
static HWND g_start, g_reset, g_list, g_status;

extern "C" {
    typedef HRESULT (WINAPI *SetPreferredAppMode_t)(int);
}

static void EnableDarkModeApis() {
    HMODULE ux = LoadLibraryExW(L"uxtheme.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!ux) return;
    auto setMode = (SetPreferredAppMode_t)GetProcAddress(ux, MAKEINTRESOURCEA(135));
    if (setMode) setMode(g_dark ? 2 : 1);
}

static void ApplyWindowChrome(HWND hwnd) {
    BOOL dark = g_dark ? TRUE : FALSE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    int corner = DWMWCP_ROUND;
    DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

    int backdrop = g_acrylic ? DWMSBT_TRANSIENTWINDOW : DWMSBT_MAINWINDOW;
    DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));
}

// ---------------------------------------------------------------------------
// ModernToggle: a self-contained, owner-drawn toggle switch control.
// (A sliding toggle is not a native Win32 control, so it must be drawn.)
// ---------------------------------------------------------------------------
struct ToggleState {
    bool on = false;
    bool hover = false;
    bool pressed = false;
    float anim = 0.0f;
    UINT_PTR timer = 0;
};

static BYTE LerpB(BYTE a, BYTE b, float t) {
    return (BYTE)(a + (b - a) * t + 0.5f);
}
static Gdiplus::Color LerpC(Gdiplus::Color a, Gdiplus::Color b, float t) {
    return Gdiplus::Color(255,
        LerpB(a.GetR(), b.GetR(), t),
        LerpB(a.GetG(), b.GetG(), t),
        LerpB(a.GetB(), b.GetB(), t));
}

static void ToggleFlip(HWND hwnd, ToggleState* s) {
    s->on = !s->on;
    if (!s->timer) s->timer = SetTimer(hwnd, 1, 12, nullptr);
    int id = GetDlgCtrlID(hwnd);
    SendMessageW(GetParent(hwnd), WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), (LPARAM)hwnd);
    InvalidateRect(hwnd, nullptr, FALSE);
}

static void TogglePaint(HWND hwnd, ToggleState* s) {
    RECT rc; GetClientRect(hwnd, &rc);
    int W = rc.right, H = rc.bottom;
    if (W <= 0 || H <= 0) return;

    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, W, H);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    FillRect(mem, &rc, g_bgBrush);

    {
        Gdiplus::Graphics g(mem);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

        Gdiplus::Color accent(255, 0, 120, 212);
        Gdiplus::Color offTrack = g_dark ? Gdiplus::Color(255, 45, 45, 45)
                                         : Gdiplus::Color(255, 240, 240, 240);
        Gdiplus::Color offKnob = g_dark ? Gdiplus::Color(255, 200, 200, 200)
                                        : Gdiplus::Color(255, 90, 90, 90);
        Gdiplus::Color onKnob(255, 255, 255, 255);
        Gdiplus::Color border = g_dark ? Gdiplus::Color(255, 150, 150, 150)
                                       : Gdiplus::Color(255, 140, 140, 140);

        float t = s->anim;
        float fW = (float)W - 1, fH = (float)H - 1;
        float radius = fH / 2.0f;

        Gdiplus::GraphicsPath track;
        track.AddArc(0.5f, 0.5f, fH, fH, 90, 180);
        track.AddArc(fW - fH, 0.5f, fH, fH, 270, 180);
        track.CloseFigure();

        Gdiplus::SolidBrush trackBrush(LerpC(offTrack, accent, t));
        g.FillPath(&trackBrush, &track);

        if (t < 0.99f) {
            Gdiplus::Color b = border;
            Gdiplus::Pen pen(Gdiplus::Color((BYTE)(b.GetA() * (1 - t)),
                b.GetR(), b.GetG(), b.GetB()), 1.0f);
            g.DrawPath(&pen, &track);
        }

        float inset = H * 0.18f;
        float knobD = H - 2 * inset;
        if (s->hover && !s->pressed) { inset -= 1.0f; knobD += 2.0f; }
        float startX = inset;
        float endX = W - inset - knobD;
        float kx = startX + (endX - startX) * t;

        Gdiplus::SolidBrush knobBrush(LerpC(offKnob, onKnob, t));
        g.FillEllipse(&knobBrush, kx, inset, knobD, knobD);
    }

    BitBlt(hdc, 0, 0, W, H, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK ToggleProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    ToggleState* s = (ToggleState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg) {
    case WM_NCCREATE:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)new ToggleState());
        return TRUE;
    case WM_DESTROY:
        if (s) { if (s->timer) KillTimer(hwnd, s->timer); delete s; }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        TogglePaint(hwnd, s);
        return 0;
    case WM_TIMER: {
        float target = s->on ? 1.0f : 0.0f;
        float d = target - s->anim;
        if (d < 0.02f && d > -0.02f) { s->anim = target; KillTimer(hwnd, s->timer); s->timer = 0; }
        else s->anim += d * 0.35f;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (!s->hover) {
            s->hover = true;
            TRACKMOUSEEVENT t{ sizeof(t), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&t);
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSELEAVE:
        s->hover = false;
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        s->pressed = true;
        SetCapture(hwnd);
        SetFocus(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_LBUTTONUP:
        if (s->pressed) {
            s->pressed = false;
            ReleaseCapture();
            RECT rc; GetClientRect(hwnd, &rc);
            POINT p{ GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
            if (PtInRect(&rc, p)) ToggleFlip(hwnd, s);
        }
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_SPACE) ToggleFlip(hwnd, s);
        return 0;
    case WM_GETDLGCODE:
        return DLGC_BUTTON;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static void RegisterToggleClass(HINSTANCE hInst) {
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = ToggleProc;
    wc.hInstance = hInst;
    wc.lpszClassName = L"ModernToggle";
    wc.hCursor = LoadCursorW(nullptr, IDC_HAND);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);
}

static void Toggle_SetCheck(HWND hwnd, bool on) {
    auto s = (ToggleState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (!s) return;
    s->on = on;
    s->anim = on ? 1.0f : 0.0f;
    InvalidateRect(hwnd, nullptr, FALSE);
}
static bool Toggle_GetCheck(HWND hwnd) {
    auto s = (ToggleState*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    return s && s->on;
}

// ---------------------------------------------------------------------------

static HFONT MakeFont(int pt, int weight, UINT dpi) {
    int height = -MulDiv(pt, dpi, 72);
    return CreateFontW(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI Variable Text");
}

static void SetFontFor(HWND parent) {
    EnumChildWindows(parent, [](HWND child, LPARAM lp) -> BOOL {
        SendMessageW(child, WM_SETFONT, (WPARAM)lp, TRUE);
        return TRUE;
    }, (LPARAM)g_uiFont);
}

static void ThemeChildren(HWND parent) {
    EnumChildWindows(parent, [](HWND child, LPARAM) -> BOOL {
        SetWindowTheme(child, g_dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
        return TRUE;
    }, 0);
}

static HWND MakeLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
        x, y, w, h, parent, nullptr, nullptr, nullptr);
}

static HWND MakeToggle(HWND parent, HINSTANCE hInst, int id, int x, int y, int w, int h) {
    return CreateWindowExW(0, L"ModernToggle", nullptr,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        x, y, w, h, parent, (HMENU)(INT_PTR)id, hInst, nullptr);
}

static void CreateControls(HWND hwnd) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    UINT dpi = GetDpiForWindow(hwnd);
    const int fieldX = 220, fieldW = 360;
    const int tw = MulDiv(46, dpi, 96), th = MulDiv(24, dpi, 96);

    g_list = CreateWindowExW(0, WC_LISTBOXW, nullptr,
        WS_CHILD | WS_VISIBLE | LBS_NOTIFY | WS_VSCROLL,
        20, 56, 170, 400, hwnd, (HMENU)ctl::ID_NAV, hInst, nullptr);
    const wchar_t* navItems[] = { L"  \x2302   Home", L"  \x2699   Settings",
        L"  \x2261   Documents", L"  \x2139   About" };
    for (auto s : navItems) SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)s);
    SendMessageW(g_list, LB_SETCURSEL, 0, 0);

    int y = 56;
    MakeLabel(hwnd, L"Display name", fieldX, y, fieldW, 20);
    g_name = CreateWindowExW(0, WC_EDITW, L"Ada Lovelace",
        WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL,
        fieldX, y + 22, fieldW, 28, hwnd, (HMENU)ctl::ID_NAME_EDIT, hInst, nullptr);
    y += 60;

    MakeLabel(hwnd, L"Role", fieldX, y, fieldW, 20);
    g_role = CreateWindowExW(0, WC_COMBOBOXW, nullptr,
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        fieldX, y + 22, fieldW, 200, hwnd, (HMENU)ctl::ID_ROLE_COMBO, hInst, nullptr);
    for (auto s : { L"Administrator", L"Developer", L"Designer", L"Guest" })
        SendMessageW(g_role, CB_ADDSTRING, 0, (LPARAM)s);
    SendMessageW(g_role, CB_SETCURSEL, 1, 0);
    y += 64;

    auto toggleRow = [&](const wchar_t* text, int id, bool on) {
        MakeLabel(hwnd, text, fieldX, y + 3, fieldW - tw - 12, 22);
        HWND t = MakeToggle(hwnd, hInst, id, fieldX + fieldW - tw, y, tw, th);
        Toggle_SetCheck(t, on);
        y += 40;
        return t;
    };
    g_notify = toggleRow(L"Enable notifications", ctl::ID_NOTIFY, true);
    g_darkChk = toggleRow(L"Dark mode", ctl::ID_DARK, g_dark);
    g_backdrop = toggleRow(L"Acrylic backdrop (off = Mica)", ctl::ID_BACKDROP, g_acrylic);
    y += 8;

    MakeLabel(hwnd, L"Volume", fieldX, y, fieldW, 20);
    g_volume = CreateWindowExW(0, TRACKBAR_CLASSW, nullptr,
        WS_CHILD | WS_VISIBLE | TBS_HORZ | TBS_AUTOTICKS,
        fieldX, y + 22, fieldW, 32, hwnd, (HMENU)ctl::ID_VOLUME_TRACK, hInst, nullptr);
    SendMessageW(g_volume, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(g_volume, TBM_SETPOS, TRUE, 65);
    y += 60;

    g_progress = CreateWindowExW(0, PROGRESS_CLASSW, nullptr,
        WS_CHILD | WS_VISIBLE,
        fieldX, y, fieldW, 8, hwnd, (HMENU)ctl::ID_PROGRESS, hInst, nullptr);
    SendMessageW(g_progress, PBM_SETRANGE, 0, MAKELPARAM(0, 100));
    SendMessageW(g_progress, PBM_SETPOS, 65, 0);
    y += 28;

    g_start = CreateWindowExW(0, WC_BUTTONW, L"Start",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        fieldX, y, 120, 34, hwnd, (HMENU)ctl::ID_START_BTN, hInst, nullptr);
    g_reset = CreateWindowExW(0, WC_BUTTONW, L"Reset",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        fieldX + 132, y, 120, 34, hwnd, (HMENU)ctl::ID_RESET_BTN, hInst, nullptr);
    y += 48;

    g_status = CreateWindowExW(0, WC_STATICW, L"Ready.",
        WS_CHILD | WS_VISIBLE,
        fieldX, y, fieldW, 22, hwnd, (HMENU)ctl::ID_STATUS, hInst, nullptr);
}

static void RebuildFonts(HWND hwnd, UINT dpi) {
    if (g_uiFont) DeleteObject(g_uiFont);
    if (g_titleFont) DeleteObject(g_titleFont);
    g_uiFont = MakeFont(10, FW_NORMAL, dpi);
    g_titleFont = MakeFont(20, FW_SEMIBOLD, dpi);
    SetFontFor(hwnd);
    SendMessageW(g_list, WM_SETFONT, (WPARAM)g_uiFont, TRUE);
}

static void ApplyTheme(HWND hwnd) {
    if (g_dark) {
        g_bg = RGB(32, 32, 32); g_fg = RGB(240, 240, 240); g_subFg = RGB(170, 170, 170);
    } else {
        g_bg = RGB(243, 243, 243); g_fg = RGB(20, 20, 20); g_subFg = RGB(90, 90, 90);
    }
    if (g_bgBrush) DeleteObject(g_bgBrush);
    g_bgBrush = CreateSolidBrush(g_bg);
    EnableDarkModeApis();
    ApplyWindowChrome(hwnd);
    ThemeChildren(hwnd);
    InvalidateRect(hwnd, nullptr, TRUE);
    RedrawWindow(hwnd, nullptr, nullptr,
        RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_ERASE);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        UINT dpi = GetDpiForWindow(hwnd);
        EnableDarkModeApis();
        CreateControls(hwnd);
        RebuildFonts(hwnd, dpi);
        ApplyTheme(hwnd);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)wParam;
        SetTextColor(dc, (HWND)lParam == g_status ? g_subFg : g_fg);
        SetBkMode(dc, TRANSPARENT);
        return (LRESULT)g_bgBrush;
    }
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLOREDIT: {
        HDC dc = (HDC)wParam;
        SetTextColor(dc, g_fg);
        SetBkColor(dc, g_dark ? RGB(45, 45, 45) : RGB(255, 255, 255));
        SetBkMode(dc, OPAQUE);
        return (LRESULT)(g_dark ? CreateSolidBrush(RGB(45, 45, 45)) : GetStockObject(WHITE_BRUSH));
    }
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(hwnd, &rc);
        FillRect((HDC)wParam, &rc, g_bgBrush);
        return 1;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        HFONT old = (HFONT)SelectObject(dc, g_titleFont);
        SetTextColor(dc, g_fg);
        SetBkMode(dc, TRANSPARENT);
        RECT title{ 220, 12, 600, 48 };
        DrawTextW(dc, L"Account settings", -1, &title, DT_LEFT | DT_SINGLELINE);
        SelectObject(dc, old);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_COMMAND: {
        int id = LOWORD(wParam), code = HIWORD(wParam);
        if (id == ctl::ID_START_BTN && code == BN_CLICKED) {
            wchar_t name[128]; GetWindowTextW(g_name, name, 128);
            std::wstring msg = L"Started session for " + std::wstring(name) + L".";
            SetWindowTextW(g_status, msg.c_str());
        } else if (id == ctl::ID_RESET_BTN && code == BN_CLICKED) {
            SetWindowTextW(g_name, L"");
            SendMessageW(g_volume, TBM_SETPOS, TRUE, 0);
            SendMessageW(g_progress, PBM_SETPOS, 0, 0);
            SetWindowTextW(g_status, L"Reset complete.");
        } else if (id == ctl::ID_NOTIFY && code == BN_CLICKED) {
            SetWindowTextW(g_status, Toggle_GetCheck(g_notify)
                ? L"Notifications enabled." : L"Notifications disabled.");
        } else if (id == ctl::ID_DARK && code == BN_CLICKED) {
            g_dark = Toggle_GetCheck(g_darkChk);
            ApplyTheme(hwnd);
        } else if (id == ctl::ID_BACKDROP && code == BN_CLICKED) {
            g_acrylic = Toggle_GetCheck(g_backdrop);
            ApplyWindowChrome(hwnd);
            SetWindowTextW(g_status, g_acrylic ? L"Backdrop: Acrylic." : L"Backdrop: Mica.");
        } else if (id == ctl::ID_NAV && code == LBN_SELCHANGE) {
            int sel = (int)SendMessageW(g_list, LB_GETCURSEL, 0, 0);
            const wchar_t* names[] = { L"Home", L"Settings", L"Documents", L"About" };
            if (sel >= 0 && sel < 4) {
                std::wstring msg = std::wstring(L"Navigated to ") + names[sel] + L".";
                SetWindowTextW(g_status, msg.c_str());
            }
        }
        return 0;
    }
    case WM_HSCROLL: {
        if ((HWND)lParam == g_volume) {
            int pos = (int)SendMessageW(g_volume, TBM_GETPOS, 0, 0);
            SendMessageW(g_progress, PBM_SETPOS, pos, 0);
        }
        return 0;
    }
    case WM_DPICHANGED: {
        UINT dpi = HIWORD(wParam);
        RebuildFonts(hwnd, dpi);
        RECT* r = (RECT*)lParam;
        SetWindowPos(hwnd, nullptr, r->left, r->top,
            r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        return 0;
    }
    case WM_DESTROY:
        if (g_uiFont) DeleteObject(g_uiFont);
        if (g_titleFont) DeleteObject(g_titleFont);
        if (g_bgBrush) DeleteObject(g_bgBrush);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR, int nCmdShow) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    Gdiplus::GdiplusStartupInput gsi;
    Gdiplus::GdiplusStartup(&g_gdiToken, &gsi, nullptr);

    INITCOMMONCONTROLSEX icc{ sizeof(icc),
        ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_PROGRESS_CLASS };
    InitCommonControlsEx(&icc);

    RegisterToggleClass(hInst);

    const wchar_t* cls = L"ModernWin32Window";
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = cls;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);

    UINT dpi = GetDpiForSystem();
    int w = MulDiv(640, dpi, 96), h = MulDiv(560, dpi, 96);

    HWND hwnd = CreateWindowExW(0, cls, L"Modern Win32 UI",
        WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, w, h,
        nullptr, nullptr, hInst, nullptr);
    if (!hwnd) return 1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (IsDialogMessageW(hwnd, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    Gdiplus::GdiplusShutdown(g_gdiToken);
    return (int)msg.wParam;
}
