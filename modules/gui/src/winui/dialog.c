#include "win32.h"

static const wchar_t* DIALOG_CLASS = L"MelGuiDialog";
static bool           g_dialog_class;

static void reenable_owner(Mel_Win32_Dialog* d)
{
    Mel_Gui_Node* ow = mel_gui__node(d->owner);
    if (ow && ow->native) {
        EnableWindow((HWND)ow->native, TRUE);
        SetActiveWindow((HWND)ow->native);
    }
}

static LRESULT CALLBACK dialog_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Mel_Win32_Dialog* d = (Mel_Win32_Dialog*)mel_gui__win32_ctl(hwnd);
    Mel_Gui_Handle    h = d ? d->frame.base.handle : MEL_GUI_HANDLE_NONE;
    void*             u = mel_gui_user(h);

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
            if (d && d->frame.lifecycle.on_resize) d->frame.lifecycle.on_resize(h, cw, ch, u);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            if (!d) break;
            MINMAXINFO* mmi = (MINMAXINFO*)lp;
            if (d->frame.min_w > 0 || d->frame.min_h > 0) {
                RECT rc = { 0, 0, d->frame.min_w, d->frame.min_h };
                AdjustWindowRectEx(&rc, d->frame.style, FALSE, d->frame.ex_style);
                if (d->frame.min_w > 0) mmi->ptMinTrackSize.x = rc.right - rc.left;
                if (d->frame.min_h > 0) mmi->ptMinTrackSize.y = rc.bottom - rc.top;
            }
            if (d->frame.max_w > 0 || d->frame.max_h > 0) {
                RECT rc = { 0, 0, d->frame.max_w, d->frame.max_h };
                AdjustWindowRectEx(&rc, d->frame.style, FALSE, d->frame.ex_style);
                if (d->frame.max_w > 0) mmi->ptMaxTrackSize.x = rc.right - rc.left;
                if (d->frame.max_h > 0) mmi->ptMaxTrackSize.y = rc.bottom - rc.top;
            }
            return 0;
        }
        case WM_SETFOCUS:
            mel_gui__set_focused(h);
            if (d && d->frame.base.focus.on_focus_in) d->frame.base.focus.on_focus_in(h, u);
            return 0;
        case WM_KILLFOCUS:
            if (mel_gui_handle_eq(mel_gui_focused(), h)) mel_gui__set_focused(MEL_GUI_HANDLE_NONE);
            if (d && d->frame.base.focus.on_focus_out) d->frame.base.focus.on_focus_out(h, u);
            return 0;
        case WM_KEYDOWN:
            if (d && d->frame.base.keyboard.on_key_down) d->frame.base.keyboard.on_key_down(h, (Mel_Key)wp, u);
            return 0;
        case WM_CLOSE: {
            bool ok = (d && d->frame.lifecycle.on_close) ? d->frame.lifecycle.on_close(h, u) : true;
            if (ok) DestroyWindow(hwnd);
            return 0;
        }
        case WM_DESTROY: {
            if (d) reenable_owner(d);
            i32 result = (d && d->result_set) ? d->result : 0;
            if (d && d->on_.on_result) d->on_.on_result(h, result, u);
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

static void ensure_dialog_class(void)
{
    if (g_dialog_class) return;
    WNDCLASSEXW dc = {0};
    dc.cbSize        = sizeof dc;
    dc.style         = CS_HREDRAW | CS_VREDRAW;
    dc.lpfnWndProc   = dialog_wndproc;
    dc.hInstance     = current_hinst;
    dc.hCursor       = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    dc.hbrBackground = (HBRUSH)(UINT_PTR)(COLOR_BTNFACE + 1);
    dc.lpszClassName = DIALOG_CLASS;
    RegisterClassExW(&dc);
    g_dialog_class = true;
}

Mel_Gui_Handle mel_dialog_create_opt(Mel_Dialog_Opt o)
{
    ensure_dialog_class();

    Mel_Gui_Handle h = mel_gui__node_new(MEL_GUI_HANDLE_NONE, o.x, o.y, o.w, o.h, 0, o.user,
                                         false, NULL, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    DWORD style;
    if (o.undecorated) {
        style = WS_POPUP | WS_BORDER;
    } else {
        style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
        if (!o.not_resizable) style |= WS_THICKFRAME;
    }
    DWORD ex_style = WS_EX_DLGMODALFRAME;

    HWND owner_hwnd = NULL;
    Mel_Gui_Node* ow = mel_gui__node(o.owner);
    if (ow) owner_hwnd = (HWND)ow->native;

    wchar_t wbuf[1024];
    mel_gui__win32_widen(o.title, wbuf, 1024);

    i32  cw = n->width  > 0 ? n->width  : 360;
    i32  ch = n->height > 0 ? n->height : 220;
    RECT rc = { 0, 0, cw, ch };
    AdjustWindowRectEx(&rc, style, FALSE, ex_style);

    int wx = CW_USEDEFAULT, wy = CW_USEDEFAULT;
    if (owner_hwnd) {
        RECT orc;
        GetWindowRect(owner_hwnd, &orc);
        wx = orc.left + ((orc.right - orc.left) - (rc.right - rc.left)) / 2;
        wy = orc.top  + ((orc.bottom - orc.top) - (rc.bottom - rc.top)) / 2;
    } else if (n->x != 0 || n->y != 0) {
        wx = n->x; wy = n->y;
    }

    HWND hwnd = CreateWindowExW(ex_style, DIALOG_CLASS, wbuf, style,
        wx, wy, rc.right - rc.left, rc.bottom - rc.top,
        owner_hwnd, NULL, current_hinst, NULL);
    n->native = hwnd;
    if (!hwnd) return h;

    Mel_Win32_Dialog* d = (Mel_Win32_Dialog*)mel_gui__win32_alloc_ctl(hwnd, sizeof *d, h);
    if (d) {
        d->frame.base.focus    = o.focus;
        d->frame.base.keyboard = o.keyboard;
        d->frame.lifecycle     = o.lifecycle;
        d->frame.style         = style;
        d->frame.ex_style      = ex_style;
        d->frame.has_menu      = FALSE;
        d->frame.min_w         = o.min_w;
        d->frame.min_h         = o.min_h;
        d->frame.max_w         = o.max_w;
        d->frame.max_h         = o.max_h;
        d->on_                 = o.on_;
        d->owner               = o.owner;
    }

    RECT got;
    GetWindowRect(hwnd, &got);
    n->x      = got.left;
    n->y      = got.top;
    n->width  = cw;
    n->height = ch;

    if (owner_hwnd) EnableWindow(owner_hwnd, FALSE);

    mel_gui__frames_inc();
    ShowWindow(hwnd, SW_SHOWNORMAL);
    SetForegroundWindow(hwnd);
    return h;
}

void mel_dialog_close(Mel_Gui_Handle dialog, i32 result)
{
    Mel_Gui_Node* n = mel_gui__node(dialog);
    if (!n || !n->native) return;
    Mel_Win32_Dialog* d = (Mel_Win32_Dialog*)mel_gui__win32_ctl((HWND)n->native);
    if (d) { d->result = result; d->result_set = true; }
    DestroyWindow((HWND)n->native);
}
