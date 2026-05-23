#include "win32.h"

#include <gui/win32/frame.h>

static HINSTANCE g_hinst;

HINSTANCE mel_gui__win32_hinst(void)
{
    if (!g_hinst) g_hinst = GetModuleHandleW(NULL);
    return g_hinst;
}

int mel_gui__win32_widen(str8 s, wchar_t* buf, int cap)
{
    if (cap <= 0) return 0;
    if (s.len <= 0 || s.data == NULL) {
        buf[0] = 0;
        return 0;
    }
    int n = MultiByteToWideChar(CP_UTF8, 0, (const char*)s.data, (int)s.len, buf, cap - 1);
    if (n < 0) n = 0;
    buf[n] = 0;
    return n;
}

size mel_gui__win32_narrow(const wchar_t* w, int wlen, char* buf, size cap)
{
    if (!buf || cap <= 0) return 0;
    if (wlen <= 0) {
        buf[0] = 0;
        return 0;
    }
    int n = WideCharToMultiByte(CP_UTF8, 0, w, wlen, buf, (int)cap - 1, NULL, NULL);
    if (n < 0) n = 0;
    buf[n] = 0;
    return (size)n;
}

Mel_Gui_Handle mel_gui__win32_handle_of(HWND hwnd)
{
    if (!hwnd) return MEL_GUI_HANDLE_NONE;
    return mel_gui_handle_unpack((u64)GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

void mel_gui__win32_bind(HWND hwnd, Mel_Gui_Handle h)
{
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)mel_gui_handle_pack(h));
}

HWND mel_gui__win32_parent_hwnd(Mel_Gui_Widget* w)
{
    Mel_Gui_Widget* p = mel_gui__get(w->parent);
    return p ? (HWND)p->native : NULL;
}

DWORD mel_gui__win32_child_style(Mel_Gui_Widget* w)
{
    DWORD style = WS_CHILD;
    if (!w->hidden)  style |= WS_VISIBLE;
    if (w->disabled) style |= WS_DISABLED;
    return style;
}

static Mel_Key vk_to_key(WPARAM vk)
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

static void traverse_tab(HWND hwnd)
{
    HWND parent = GetParent(hwnd);
    if (!parent) return;
    BOOL shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    HWND next  = GetNextDlgTabItem(parent, hwnd, shift);
    if (next && next != hwnd) SetFocus(next);
}

bool mel_gui__win32_subclass_common(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, Mel_Gui_Handle h)
{
    (void)lp;
    switch (msg) {
        case WM_SETFOCUS:
            mel_gui__set_focused(h);
            mel_gui__fire_focus_in(h);
            return false;
        case WM_KILLFOCUS:
            mel_gui__set_focused(MEL_GUI_HANDLE_NONE);
            mel_gui__fire_focus_out(h);
            return false;
        case WM_KEYDOWN:
            if (wp == VK_TAB) {
                traverse_tab(hwnd);
                return true;
            }
            mel_gui__fire_key_down(h, vk_to_key(wp));
            return false;
        case WM_KEYUP:
            mel_gui__fire_key_up(h, vk_to_key(wp));
            return false;
        case WM_CHAR:
            if (wp == VK_TAB) return true;
            mel_gui__fire_char(h, (u32)wp);
            return false;
        default:
            return false;
    }
}

bool mel_gui__backend_init(void)
{
    INITCOMMONCONTROLSEX icc = { sizeof icc, ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);
    return true;
}

void mel_gui__backend_destroy(Mel_Gui_Widget* w)
{
    if (w && w->native) {
        HWND hwnd = (HWND)w->native;
        w->native = NULL;
        DestroyWindow(hwnd);
    }
}

void mel_gui__backend_set_text(Mel_Gui_Widget* w, str8 text)
{
    if (!w || !w->native) return;
    wchar_t wbuf[2048];
    mel_gui__win32_widen(text, wbuf, 2048);
    SetWindowTextW((HWND)w->native, wbuf);
}

size mel_gui__backend_get_text(Mel_Gui_Widget* w, char* buf, size cap)
{
    if (buf && cap > 0) buf[0] = 0;
    if (!w || !w->native || !buf || cap <= 0) return 0;
    wchar_t wbuf[2048];
    int n = GetWindowTextW((HWND)w->native, wbuf, 2048);
    return mel_gui__win32_narrow(wbuf, n, buf, cap);
}

void mel_gui__backend_set_bounds(Mel_Gui_Widget* w, i32 x, i32 y, i32 width, i32 height)
{
    if (!w || !w->native) return;
    if (mel_gui__is_toplevel(w)) {
        Mel_Gui_Frame_Impl* fi = (Mel_Gui_Frame_Impl*)w->impl;
        DWORD style    = fi ? (DWORD)fi->native_style    : WS_OVERLAPPEDWINDOW;
        DWORD ex_style = fi ? (DWORD)fi->native_ex_style : 0;
        BOOL  has_menu = fi ? (BOOL)fi->has_menu         : FALSE;
        RECT  rc       = { 0, 0, width, height };
        AdjustWindowRectEx(&rc, style, has_menu, ex_style);
        MoveWindow((HWND)w->native, x, y,
                   rc.right - rc.left, rc.bottom - rc.top, TRUE);
    } else {
        MoveWindow((HWND)w->native, x, y, width, height, TRUE);
    }
}

void mel_gui__backend_set_visible(Mel_Gui_Widget* w, bool visible)
{
    if (!w || !w->native) return;
    HWND hwnd = (HWND)w->native;
    if (!visible) {
        ShowWindow(hwnd, SW_HIDE);
        return;
    }
    int ncmd = SW_SHOW;
    if (mel_gui__is_toplevel(w)) {
        Mel_Gui_Frame_Impl* fi = (Mel_Gui_Frame_Impl*)w->impl;
        if (fi && !fi->first_show_done) {
            switch (fi->initial_state) {
                case MEL_FRAME_MINIMIZED: ncmd = SW_SHOWMINIMIZED; break;
                case MEL_FRAME_MAXIMIZED: ncmd = SW_SHOWMAXIMIZED; break;
                default:                  ncmd = SW_SHOWNORMAL;    break;
            }
            fi->first_show_done = true;
        }
    }
    ShowWindow(hwnd, ncmd);
}

void mel_gui__backend_set_enabled(Mel_Gui_Widget* w, bool enabled)
{
    if (w && w->native) {
        EnableWindow((HWND)w->native, enabled);
    }
}

void mel_gui__backend_set_focus(Mel_Gui_Widget* w)
{
    if (!w || !w->native) return;
    if (mel_gui__is_toplevel(w)) {
        SetForegroundWindow((HWND)w->native);
    } else {
        SetFocus((HWND)w->native);
    }
}

void mel_gui__backend_invalidate(Mel_Gui_Widget* w)
{
    if (w && w->native) {
        InvalidateRect((HWND)w->native, NULL, TRUE);
    }
}

HWND mel_gui_win32_hwnd(Mel_Gui_Handle h)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    return w ? (HWND)w->native : NULL;
}

bool mel_gui_win32_install_subclass(Mel_Gui_Handle h, SUBCLASSPROC proc,
                                    UINT_PTR id, DWORD_PTR ref)
{
    HWND hwnd = mel_gui_win32_hwnd(h);
    if (!hwnd || !proc) return false;
    return SetWindowSubclass(hwnd, proc, id, ref) != FALSE;
}

bool mel_gui_win32_remove_subclass(Mel_Gui_Handle h, SUBCLASSPROC proc, UINT_PTR id)
{
    HWND hwnd = mel_gui_win32_hwnd(h);
    if (!hwnd || !proc) return false;
    return RemoveWindowSubclass(hwnd, proc, id) != FALSE;
}
