#include "win32.h"

static const wchar_t* SCROLL_CLASS = L"MelGuiScroll";
static bool           g_scroll_class;

static void scroll_apply(HWND hwnd, Mel_Win32_Scroll* s)
{
    if (!s) return;
    RECT rc;
    GetClientRect(hwnd, &rc);
    i32 pw = rc.right - rc.left;
    i32 ph = rc.bottom - rc.top;

    i32 maxx = s->content_w - pw; if (maxx < 0) maxx = 0;
    i32 maxy = s->content_h - ph; if (maxy < 0) maxy = 0;
    if (s->pos_x > maxx) s->pos_x = maxx;
    if (s->pos_x < 0)    s->pos_x = 0;
    if (s->pos_y > maxy) s->pos_y = maxy;
    if (s->pos_y < 0)    s->pos_y = 0;

    SCROLLINFO si = { sizeof si };
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax  = s->content_h > 0 ? s->content_h - 1 : 0;
    si.nPage = (UINT)ph;
    si.nPos  = s->pos_y;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
    si.nMax  = s->content_w > 0 ? s->content_w - 1 : 0;
    si.nPage = (UINT)pw;
    si.nPos  = s->pos_x;
    SetScrollInfo(hwnd, SB_HORZ, &si, TRUE);

    SetWindowPos(s->inner, NULL, -s->pos_x, -s->pos_y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
}

static i32 scroll_calc(HWND hwnd, int bar, WPARAM wp, i32 cur, i32 page)
{
    switch (LOWORD(wp)) {
        case SB_LINEUP:   cur -= 16;   break;
        case SB_LINEDOWN: cur += 16;   break;
        case SB_PAGEUP:   cur -= page; break;
        case SB_PAGEDOWN: cur += page; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: {
            SCROLLINFO si = { sizeof si };
            si.fMask = SIF_TRACKPOS;
            GetScrollInfo(hwnd, bar, &si);
            cur = si.nTrackPos;
            break;
        }
        default: break;
    }
    return cur;
}

static LRESULT CALLBACK scroll_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Mel_Win32_Scroll* s = (Mel_Win32_Scroll*)mel_gui__win32_ctl(hwnd);

    switch (msg) {
        case WM_VSCROLL:
            if (lp) { SendMessageW((HWND)lp, MEL_REFLECT(WM_VSCROLL), wp, lp); return 0; }
            if (s) {
                RECT rc; GetClientRect(hwnd, &rc);
                s->pos_y = scroll_calc(hwnd, SB_VERT, wp, s->pos_y, rc.bottom - rc.top);
                scroll_apply(hwnd, s);
            }
            return 0;
        case WM_HSCROLL:
            if (lp) { SendMessageW((HWND)lp, MEL_REFLECT(WM_HSCROLL), wp, lp); return 0; }
            if (s) {
                RECT rc; GetClientRect(hwnd, &rc);
                s->pos_x = scroll_calc(hwnd, SB_HORZ, wp, s->pos_x, rc.right - rc.left);
                scroll_apply(hwnd, s);
            }
            return 0;
        case WM_MOUSEWHEEL:
            if (s) {
                i32 delta = GET_WHEEL_DELTA_WPARAM(wp);
                s->pos_y -= (delta / WHEEL_DELTA) * 48;
                scroll_apply(hwnd, s);
            }
            return 0;
        case WM_COMMAND:
            if (lp) { SendMessageW((HWND)lp, MEL_REFLECT(WM_COMMAND), wp, lp); return 0; }
            break;
        case WM_CTLCOLORSTATIC:
            SetBkMode((HDC)wp, TRANSPARENT);
            return (LRESULT)(UINT_PTR)GetSysColorBrush(COLOR_BTNFACE);
        case WM_SIZE:
            scroll_apply(hwnd, s);
            return 0;
        case WM_NCDESTROY:
            mel_gui__win32_free_ctl(hwnd);
            break;
        default:
            break;
    }
    if (mel_gui__win32_subclass_common(hwnd, msg, wp, lp)) return 0;
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void ensure_scroll_class(void)
{
    if (g_scroll_class) return;
    WNDCLASSEXW sc = {0};
    sc.cbSize        = sizeof sc;
    sc.style         = CS_HREDRAW | CS_VREDRAW;
    sc.lpfnWndProc   = scroll_wndproc;
    sc.hInstance     = current_hinst;
    sc.hCursor       = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    sc.hbrBackground = (HBRUSH)(UINT_PTR)(COLOR_BTNFACE + 1);
    sc.lpszClassName = SCROLL_CLASS;
    RegisterClassExW(&sc);
    g_scroll_class = true;
}

Mel_Gui_Handle mel_scrollview_create_opt(Mel_Gui_Handle parent, Mel_ScrollView_Opt o)
{
    ensure_scroll_class();

    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    HWND par = mel_gui__win32_parent_hwnd(n);
    if (!par) return h;

    DWORD style = mel_gui__win32_child_style(n, o.disabled) | WS_VSCROLL | WS_CLIPCHILDREN;
    if (o.content_w > 0) style |= WS_HSCROLL;

    HWND outer = CreateWindowExW(0, SCROLL_CLASS, NULL, style,
        n->x, n->y, n->width, n->height, par, NULL, current_hinst, NULL);
    n->native = outer;
    if (!outer) return h;

    i32 cw = o.content_w > 0 ? o.content_w : n->width;
    i32 ch = o.content_h > 0 ? o.content_h : n->height;

    HWND inner = mel_gui__win32_make_container(outer, 0, 0, cw, ch, h,
        (Mel_Gui_Pointer_Cb){0}, o.focus, (Mel_Gui_Keyboard_Cb){0}, false, false);

    Mel_Win32_Scroll* s = (Mel_Win32_Scroll*)mel_gui__win32_alloc_ctl(outer, sizeof *s, h);
    if (s) {
        s->base.focus = o.focus;
        s->inner      = inner;
        s->content_w  = cw;
        s->content_h  = ch;
    }
    n->content = inner;

    scroll_apply(outer, s);
    return h;
}
