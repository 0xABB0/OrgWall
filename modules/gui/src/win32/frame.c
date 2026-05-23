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
    Mel_Win32_Frame* f = (Mel_Win32_Frame*)mel_gui__win32_ctl(hwnd);
    Mel_Gui_Handle   h = f ? f->base.handle : MEL_GUI_HANDLE_NONE;
    void*            u = mel_gui_user(h);

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
        case WM_SIZE: {
            i32 cw = (i32)LOWORD(lp);
            i32 ch = (i32)HIWORD(lp);
            mel_gui__resized(h, cw, ch);
            if (f && f->lifecycle.on_resize) f->lifecycle.on_resize(h, cw, ch, u);
            return 0;
        }
        case WM_MOVE: {
            Mel_Gui_Node* n = mel_gui__node(h);
            if (n) { RECT rc; GetWindowRect(hwnd, &rc); n->x = rc.left; n->y = rc.top; }
            return 0;
        }
        case WM_SHOWWINDOW:
            if (f) {
                if (wp) { if (f->lifecycle.on_show) f->lifecycle.on_show(h, u); }
                else    { if (f->lifecycle.on_hide) f->lifecycle.on_hide(h, u); }
            }
            return 0;
        case WM_ENABLE:
            if (f && f->lifecycle.on_enable) f->lifecycle.on_enable(h, wp != 0, u);
            return 0;
        case WM_GETMINMAXINFO: {
            if (!f) break;
            MINMAXINFO* mmi = (MINMAXINFO*)lp;
            if (f->min_w > 0 || f->min_h > 0) {
                RECT rc = { 0, 0, f->min_w > 0 ? f->min_w : 0, f->min_h > 0 ? f->min_h : 0 };
                AdjustWindowRectEx(&rc, f->style, f->has_menu, f->ex_style);
                if (f->min_w > 0) mmi->ptMinTrackSize.x = rc.right - rc.left;
                if (f->min_h > 0) mmi->ptMinTrackSize.y = rc.bottom - rc.top;
            }
            if (f->max_w > 0 || f->max_h > 0) {
                RECT rc = { 0, 0, f->max_w > 0 ? f->max_w : 0, f->max_h > 0 ? f->max_h : 0 };
                AdjustWindowRectEx(&rc, f->style, f->has_menu, f->ex_style);
                if (f->max_w > 0) mmi->ptMaxTrackSize.x = rc.right - rc.left;
                if (f->max_h > 0) mmi->ptMaxTrackSize.y = rc.bottom - rc.top;
            }
            return 0;
        }
        case WM_SETFOCUS:
            mel_gui__set_focused(h);
            if (f && f->base.focus.on_focus_in) f->base.focus.on_focus_in(h, u);
            return 0;
        case WM_KILLFOCUS:
            if (mel_gui_handle_eq(mel_gui_focused(), h)) mel_gui__set_focused(MEL_GUI_HANDLE_NONE);
            if (f && f->base.focus.on_focus_out) f->base.focus.on_focus_out(h, u);
            return 0;
        case WM_KEYDOWN:
            if (f && f->base.keyboard.on_key_down) f->base.keyboard.on_key_down(h, vk_to_key_frame(wp), u);
            return 0;
        case WM_KEYUP:
            if (f && f->base.keyboard.on_key_up) f->base.keyboard.on_key_up(h, vk_to_key_frame(wp), u);
            return 0;
        case WM_CHAR:
            if (f && f->base.keyboard.on_char) f->base.keyboard.on_char(h, (u32)wp, u);
            return 0;
        case WM_CLOSE: {
            bool ok = (f && f->lifecycle.on_close) ? f->lifecycle.on_close(h, u) : true;
            if (ok) DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY: {
            if (!mel_gui_handle_is_none(h)) {
                u32           count = 0;
                Mel_Gui_Node* data  = mel_gui__nodes(&count);
                for (u32 i = 0; i < count; i++) {
                    if (mel_gui_handle_eq(data[i].self, h) || mel_gui_handle_eq(data[i].parent, h)) {
                        data[i].native = NULL;
                    }
                }
                mel_gui__destroy_tree(h);
            }
            if (mel_gui__frames_dec() == 0) PostQuitMessage(0);
            return 0;
        }
        case WM_NCDESTROY:
            mel_gui__win32_free_ctl(hwnd);
            return DefWindowProcW(hwnd, msg, wp, lp);
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

Mel_Gui_Handle mel_frame_create_opt(Mel_Frame_Opt o)
{
    ensure_frame_class();

    Mel_Gui_Handle h = mel_gui__node_new(MEL_GUI_HANDLE_NONE, o.x, o.y, o.w, o.h, 0, o.user,
                                         o.initial_state == MEL_FRAME_HIDDEN, NULL, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    DWORD style;
    if (o.undecorated) {
        style = WS_POPUP;
    } else {
        style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        if (!o.not_resizable) style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
    }
    DWORD ex_style = 0;

    HWND owner_hwnd = NULL;
    Mel_Gui_Node* ow = mel_gui__node(o.owner);
    if (ow) owner_hwnd = (HWND)ow->native;

    wchar_t wbuf[1024];
    mel_gui__win32_widen(o.title, wbuf, 1024);

    i32  cw = n->width  > 0 ? n->width  : 480;
    i32  ch = n->height > 0 ? n->height : 360;
    RECT rc = { 0, 0, cw, ch };
    AdjustWindowRectEx(&rc, style, FALSE, ex_style);

    bool use_default_pos = (n->x == 0 && n->y == 0);
    int  wx = use_default_pos ? CW_USEDEFAULT : n->x;
    int  wy = use_default_pos ? CW_USEDEFAULT : n->y;

    HWND hwnd = CreateWindowExW(ex_style, FRAME_CLASS, wbuf, style,
        wx, wy, rc.right - rc.left, rc.bottom - rc.top,
        owner_hwnd, NULL, current_hinst, NULL);
    n->native = hwnd;
    if (!hwnd) return h;

    Mel_Win32_Frame* f = (Mel_Win32_Frame*)mel_gui__win32_alloc_ctl(hwnd, sizeof *f, h);
    if (f) {
        f->base.focus    = o.focus;
        f->base.keyboard = o.keyboard;
        f->lifecycle     = o.lifecycle;
        f->style         = style;
        f->ex_style      = ex_style;
        f->has_menu      = FALSE;
        f->min_w         = o.min_w;
        f->min_h         = o.min_h;
        f->max_w         = o.max_w;
        f->max_h         = o.max_h;
        f->initial_state = (u8)o.initial_state;
    }

    if (o.icon_large) SendMessageW(hwnd, WM_SETICON, ICON_BIG,   (LPARAM)o.icon_large);
    if (o.icon_small) SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)o.icon_small);
    if (o.not_closable) {
        HMENU sys = GetSystemMenu(hwnd, FALSE);
        if (sys) EnableMenuItem(sys, SC_CLOSE, MF_BYCOMMAND | MF_GRAYED);
    }

    RECT got;
    GetWindowRect(hwnd, &got);
    n->x      = got.left;
    n->y      = got.top;
    n->width  = cw;
    n->height = ch;

    mel_gui__frames_inc();
    return h;
}
