#include "win32.h"

static const wchar_t* FRAME_CLASS = L"MelGuiFrame";
static bool           g_frame_class;

static Mel_Key vk_to_key_frame(WPARAM vk)
{
    if (vk >= '0' && vk <= '9') return (Mel_Key)vk;
    if (vk >= 'A' && vk <= 'Z') return (Mel_Key)vk;
    switch (vk) {
        case VK_BACK:    return MEL_KEY_BACKSPACE;
        case VK_TAB:     return MEL_KEY_TAB;
        case VK_RETURN:  return MEL_KEY_ENTER;
        case VK_ESCAPE:  return MEL_KEY_ESCAPE;
        case VK_SPACE:   return MEL_KEY_SPACE;
        case VK_LEFT:    return MEL_KEY_LEFT;
        case VK_RIGHT:   return MEL_KEY_RIGHT;
        case VK_UP:      return MEL_KEY_UP;
        case VK_DOWN:    return MEL_KEY_DOWN;
        case VK_HOME:    return MEL_KEY_HOME;
        case VK_END:     return MEL_KEY_END;
        case VK_PRIOR:   return MEL_KEY_PAGE_UP;
        case VK_NEXT:    return MEL_KEY_PAGE_DOWN;
        case VK_INSERT:  return MEL_KEY_INSERT;
        case VK_DELETE:  return MEL_KEY_DELETE;
        case VK_SHIFT:   return MEL_KEY_SHIFT;
        case VK_CONTROL: return MEL_KEY_CONTROL;
        case VK_MENU:    return MEL_KEY_ALT;
        case VK_F1:  return MEL_KEY_F1;  case VK_F2:  return MEL_KEY_F2;
        case VK_F3:  return MEL_KEY_F3;  case VK_F4:  return MEL_KEY_F4;
        case VK_F5:  return MEL_KEY_F5;  case VK_F6:  return MEL_KEY_F6;
        case VK_F7:  return MEL_KEY_F7;  case VK_F8:  return MEL_KEY_F8;
        case VK_F9:  return MEL_KEY_F9;  case VK_F10: return MEL_KEY_F10;
        case VK_F11: return MEL_KEY_F11; case VK_F12: return MEL_KEY_F12;
        default:     return MEL_KEY_NONE;
    }
}

static LRESULT CALLBACK frame_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_COMMAND:
            if (lp) {
                SendMessageW((HWND)lp, MEL_REFLECT(WM_COMMAND), wp, lp);
                return 0;
            }
            break;
        case WM_HSCROLL:
            if (lp) {
                SendMessageW((HWND)lp, MEL_REFLECT(WM_HSCROLL), wp, lp);
                return 0;
            }
            break;
        case WM_CTLCOLORSTATIC:
            SetBkMode((HDC)wp, TRANSPARENT);
            return (LRESULT)(UINT_PTR)GetSysColorBrush(COLOR_BTNFACE);
        case WM_SIZE: {
            Mel_Gui_Handle  h = mel_gui__win32_handle_of(hwnd);
            Mel_Gui_Widget* w = mel_gui__get(h);
            i32 cw = (i32)LOWORD(lp);
            i32 ch = (i32)HIWORD(lp);
            if (w) {
                w->width  = cw;
                w->height = ch;
            }
            mel_gui__fire_resize(h, cw, ch);
            return 0;
        }
        case WM_MOVE: {
            Mel_Gui_Widget* w = mel_gui__get(mel_gui__win32_handle_of(hwnd));
            if (w) {
                RECT rc;
                GetWindowRect(hwnd, &rc);
                w->x = rc.left;
                w->y = rc.top;
            }
            return 0;
        }
        case WM_SHOWWINDOW: {
            Mel_Gui_Handle h = mel_gui__win32_handle_of(hwnd);
            if (wp) mel_gui__fire_show(h);
            else    mel_gui__fire_hide(h);
            return 0;
        }
        case WM_ENABLE: {
            Mel_Gui_Handle h = mel_gui__win32_handle_of(hwnd);
            mel_gui__fire_enable(h, wp != 0);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            Mel_Gui_Widget* w = mel_gui__get(mel_gui__win32_handle_of(hwnd));
            if (!w || !w->impl) break;
            Mel_Gui_Frame_Impl* fi = (Mel_Gui_Frame_Impl*)w->impl;
            MINMAXINFO* mmi = (MINMAXINFO*)lp;
            if (fi->min_w > 0 || fi->min_h > 0) {
                RECT rc = { 0, 0,
                            fi->min_w > 0 ? fi->min_w : 0,
                            fi->min_h > 0 ? fi->min_h : 0 };
                AdjustWindowRectEx(&rc, (DWORD)fi->native_style, fi->has_menu,
                                   (DWORD)fi->native_ex_style);
                if (fi->min_w > 0) mmi->ptMinTrackSize.x = rc.right - rc.left;
                if (fi->min_h > 0) mmi->ptMinTrackSize.y = rc.bottom - rc.top;
            }
            if (fi->max_w > 0 || fi->max_h > 0) {
                RECT rc = { 0, 0,
                            fi->max_w > 0 ? fi->max_w : 0,
                            fi->max_h > 0 ? fi->max_h : 0 };
                AdjustWindowRectEx(&rc, (DWORD)fi->native_style, fi->has_menu,
                                   (DWORD)fi->native_ex_style);
                if (fi->max_w > 0) mmi->ptMaxTrackSize.x = rc.right - rc.left;
                if (fi->max_h > 0) mmi->ptMaxTrackSize.y = rc.bottom - rc.top;
            }
            return 0;
        }
        case WM_SETFOCUS: {
            Mel_Gui_Handle h = mel_gui__win32_handle_of(hwnd);
            mel_gui__set_focused(h);
            mel_gui__fire_focus_in(h);
            return 0;
        }
        case WM_KILLFOCUS: {
            Mel_Gui_Handle h = mel_gui__win32_handle_of(hwnd);
            mel_gui__set_focused(MEL_GUI_HANDLE_NONE);
            mel_gui__fire_focus_out(h);
            return 0;
        }
        case WM_KEYDOWN: {
            Mel_Gui_Handle h = mel_gui__win32_handle_of(hwnd);
            mel_gui__fire_key_down(h, vk_to_key_frame(wp));
            return 0;
        }
        case WM_KEYUP: {
            Mel_Gui_Handle h = mel_gui__win32_handle_of(hwnd);
            mel_gui__fire_key_up(h, vk_to_key_frame(wp));
            return 0;
        }
        case WM_CHAR: {
            Mel_Gui_Handle h = mel_gui__win32_handle_of(hwnd);
            mel_gui__fire_char(h, (u32)wp);
            return 0;
        }
        case WM_CLOSE: {
            Mel_Gui_Handle h = mel_gui__win32_handle_of(hwnd);
            if (mel_gui__fire_close(h)) DestroyWindow(hwnd);
            return 0;
        }
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

static void compose_style(Mel_Gui_Frame_Impl* fi, DWORD* out_style, DWORD* out_ex)
{
    DWORD style;
    if (!fi->decorated) {
        style = WS_POPUP;
    } else {
        style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        if (fi->resizable) style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
    }
    *out_style = style;
    *out_ex    = 0;
}

void mel_gui__backend_frame_create(Mel_Gui_Widget* w, str8 title)
{
    ensure_frame_class();

    Mel_Gui_Frame_Impl* fi = (Mel_Gui_Frame_Impl*)w->impl;

    DWORD style    = WS_OVERLAPPEDWINDOW;
    DWORD ex_style = 0;
    if (fi) {
        compose_style(fi, &style, &ex_style);
        fi->native_style    = (u32)style;
        fi->native_ex_style = (u32)ex_style;
    }

    HWND owner_hwnd = NULL;
    if (fi) {
        Mel_Gui_Widget* ow = mel_gui__get(fi->owner);
        if (ow) owner_hwnd = (HWND)ow->native;
    }

    wchar_t wbuf[1024];
    mel_gui__win32_widen(title, wbuf, 1024);

    i32  cw = w->width  > 0 ? w->width  : 480;
    i32  ch = w->height > 0 ? w->height : 360;
    RECT rc = { 0, 0, cw, ch };
    AdjustWindowRectEx(&rc, style, FALSE, ex_style);

    bool use_default_pos = (w->x == 0 && w->y == 0);
    int  wx = use_default_pos ? CW_USEDEFAULT : w->x;
    int  wy = use_default_pos ? CW_USEDEFAULT : w->y;

    HWND hwnd = CreateWindowExW(ex_style, FRAME_CLASS, wbuf, style,
        wx, wy, rc.right - rc.left, rc.bottom - rc.top,
        owner_hwnd, NULL, current_hinst, NULL);

    w->native = hwnd;
    if (!hwnd) return;

    mel_gui__win32_bind(hwnd, w->self);

    if (fi) {
        if (fi->icon_large) SendMessageW(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)fi->icon_large);
        if (fi->icon_small) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)fi->icon_small);
        if (!fi->closable) {
            HMENU sys = GetSystemMenu(hwnd, FALSE);
            if (sys) EnableMenuItem(sys, SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);
        }
    }

    RECT got;
    GetWindowRect(hwnd, &got);
    w->x      = got.left;
    w->y      = got.top;
    w->width  = cw;
    w->height = ch;

    mel_gui__frames_inc();
}
