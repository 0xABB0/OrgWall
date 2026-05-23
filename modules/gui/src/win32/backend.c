#include "win32.h"

#include <gui/win32/frame.h>

int mel_gui__win32_widen(str8 s, wchar_t* buf, int cap)
{
    if (cap <= 0) return 0;
    if (s.len <= 0 || s.data == NULL) { buf[0] = 0; return 0; }
    int n = MultiByteToWideChar(CP_UTF8, 0, (const char*)s.data, (int)s.len, buf, cap - 1);
    if (n < 0) n = 0;
    buf[n] = 0;
    return n;
}

size mel_gui__win32_narrow(const wchar_t* w, int wlen, char* buf, size cap)
{
    if (!buf || cap <= 0) return 0;
    if (wlen <= 0) { buf[0] = 0; return 0; }
    int n = WideCharToMultiByte(CP_UTF8, 0, w, wlen, buf, (int)cap - 1, NULL, NULL);
    if (n < 0) n = 0;
    buf[n] = 0;
    return (size)n;
}

Mel_Win32_Ctl* mel_gui__win32_ctl(HWND hwnd)
{
    if (!hwnd) return NULL;
    return (Mel_Win32_Ctl*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
}

Mel_Gui_Handle mel_gui__win32_handle_of(HWND hwnd)
{
    Mel_Win32_Ctl* c = mel_gui__win32_ctl(hwnd);
    return c ? c->handle : MEL_GUI_HANDLE_NONE;
}

void* mel_gui__win32_alloc_ctl(HWND hwnd, usize size, Mel_Gui_Handle h)
{
    Mel_Win32_Ctl* c = (Mel_Win32_Ctl*)mel_calloc(mel_gui__alloc(), size);
    if (!c) return NULL;
    c->handle = h;
    if (hwnd) SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)c);
    return c;
}

void mel_gui__win32_free_ctl(HWND hwnd)
{
    Mel_Win32_Ctl* c = mel_gui__win32_ctl(hwnd);
    if (c) {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        mel_dealloc(mel_gui__alloc(), c);
    }
}

HWND mel_gui__win32_parent_hwnd(Mel_Gui_Node* n)
{
    Mel_Gui_Node* p = mel_gui__node(n->parent);
    return p ? (HWND)p->native : NULL;
}

DWORD mel_gui__win32_child_style(Mel_Gui_Node* n, bool disabled)
{
    DWORD style = WS_CHILD;
    if (!n->hidden) style |= WS_VISIBLE;
    if (disabled)   style |= WS_DISABLED;
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

bool mel_gui__win32_subclass_common(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)lp;
    Mel_Win32_Ctl* c = mel_gui__win32_ctl(hwnd);
    if (!c) return false;
    Mel_Gui_Handle h = c->handle;
    void*          u = mel_gui_user(h);

    switch (msg) {
        case WM_SETFOCUS:
            mel_gui__set_focused(h);
            if (c->focus.on_focus_in) c->focus.on_focus_in(h, u);
            return false;
        case WM_KILLFOCUS:
            if (mel_gui_handle_eq(mel_gui_focused(), h)) mel_gui__set_focused(MEL_GUI_HANDLE_NONE);
            if (c->focus.on_focus_out) c->focus.on_focus_out(h, u);
            return false;
        case WM_KEYDOWN:
            if (wp == VK_TAB) { traverse_tab(hwnd); return true; }
            if (c->keyboard.on_key_down) c->keyboard.on_key_down(h, vk_to_key(wp), u);
            return false;
        case WM_KEYUP:
            if (c->keyboard.on_key_up) c->keyboard.on_key_up(h, vk_to_key(wp), u);
            return false;
        case WM_CHAR:
            if (wp == VK_TAB) return true;
            if (c->keyboard.on_char) c->keyboard.on_char(h, (u32)wp, u);
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

void mel_gui__backend_destroy(Mel_Gui_Node* n)
{
    if (n && n->native) {
        HWND hwnd = (HWND)n->native;
        n->native = NULL;
        DestroyWindow(hwnd);
    }
}

void mel_gui_set_text(Mel_Gui_Handle h, str8 text)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native) return;
    wchar_t wbuf[2048];
    mel_gui__win32_widen(text, wbuf, 2048);
    SetWindowTextW((HWND)n->native, wbuf);
}

size mel_gui_get_text(Mel_Gui_Handle h, char* buf, size cap)
{
    if (buf && cap > 0) buf[0] = 0;
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native || !buf || cap <= 0) return 0;
    wchar_t wbuf[2048];
    int got = GetWindowTextW((HWND)n->native, wbuf, 2048);
    return mel_gui__win32_narrow(wbuf, got, buf, cap);
}

void mel_gui_set_bounds(Mel_Gui_Handle h, i32 x, i32 y, i32 width, i32 height)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return;
    n->x = x; n->y = y; n->width = width; n->height = height;
    if (!n->native) return;
    HWND hwnd = (HWND)n->native;

    if (mel_gui__is_toplevel(n)) {
        Mel_Win32_Frame* f = (Mel_Win32_Frame*)mel_gui__win32_ctl(hwnd);
        DWORD style    = f ? f->style    : WS_OVERLAPPEDWINDOW;
        DWORD ex_style = f ? f->ex_style : 0;
        BOOL  has_menu = f ? f->has_menu : FALSE;
        RECT  rc       = { 0, 0, width, height };
        AdjustWindowRectEx(&rc, style, has_menu, ex_style);
        MoveWindow(hwnd, x, y, rc.right - rc.left, rc.bottom - rc.top, TRUE);
    } else {
        MoveWindow(hwnd, x, y, width, height, TRUE);
    }
}

void mel_gui_set_visible(Mel_Gui_Handle h, bool visible)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return;
    n->hidden = !visible;
    if (!n->native) return;
    HWND hwnd = (HWND)n->native;

    if (!visible) { ShowWindow(hwnd, SW_HIDE); return; }

    int ncmd = SW_SHOW;
    if (mel_gui__is_toplevel(n)) {
        Mel_Win32_Frame* f = (Mel_Win32_Frame*)mel_gui__win32_ctl(hwnd);
        if (f && !f->first_show_done) {
            switch (f->initial_state) {
                case MEL_FRAME_MINIMIZED: ncmd = SW_SHOWMINIMIZED; break;
                case MEL_FRAME_MAXIMIZED: ncmd = SW_SHOWMAXIMIZED; break;
                default:                  ncmd = SW_SHOWNORMAL;    break;
            }
            f->first_show_done = true;
        }
    }
    ShowWindow(hwnd, ncmd);
}

void mel_gui_set_enabled(Mel_Gui_Handle h, bool enabled)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (n && n->native) EnableWindow((HWND)n->native, enabled);
}

void mel_gui_set_focus(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native) return;
    if (mel_gui__is_toplevel(n)) SetForegroundWindow((HWND)n->native);
    else                         SetFocus((HWND)n->native);
}

void mel_gui_invalidate(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (n && n->native) InvalidateRect((HWND)n->native, NULL, TRUE);
}

HWND mel_gui_win32_hwnd(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    return n ? (HWND)n->native : NULL;
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
