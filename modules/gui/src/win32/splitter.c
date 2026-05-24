#include "win32.h"

#define DIVIDER_THICK 6

static const wchar_t* SPLITTER_CLASS = L"MelGuiSplitter";
static bool           g_splitter_class;

static void split_relayout(Mel_Win32_Splitter* s)
{
    if (!s || s->pane_count <= 0) return;
    HWND hwnd = (HWND)mel_gui_native_handle(s->base.handle);
    if (!hwnd) return;

    RECT rc;
    GetClientRect(hwnd, &rc);
    i32 cw = rc.right - rc.left;
    i32 ch = rc.bottom - rc.top;
    i32 axis  = s->vertical ? ch : cw;
    i32 cross = s->vertical ? cw : ch;

    i32 total_div = DIVIDER_THICK * (s->pane_count - 1);
    i32 avail = axis - total_div;
    if (avail < 0) avail = 0;

    i32 fixed = 0, zeros = 0;
    for (i32 i = 0; i < s->pane_count; i++) {
        if (s->sizes[i] > 0) fixed += s->sizes[i];
        else                 zeros++;
    }
    if (zeros > 0) {
        i32 rem  = avail - fixed; if (rem < 0) rem = 0;
        i32 each = rem / zeros;
        for (i32 i = 0; i < s->pane_count; i++)
            if (s->sizes[i] <= 0) s->sizes[i] = each > 0 ? each : 1;
    }

    i32 sum = 0;
    for (i32 i = 0; i < s->pane_count; i++) sum += s->sizes[i];
    if (sum <= 0) sum = 1;

    i32 used = 0;
    for (i32 i = 0; i < s->pane_count; i++) {
        i32 ext = (i == s->pane_count - 1) ? (avail - used)
                                           : (s->sizes[i] * avail / sum);
        if (ext < 0) ext = 0;
        s->sizes[i] = ext;

        if (s->vertical) MoveWindow(s->panes[i], 0, used, cross, ext, TRUE);
        else             MoveWindow(s->panes[i], used, 0, ext, cross, TRUE);

        Mel_Gui_Node* pn = mel_gui__node(mel_gui__win32_handle_of(s->panes[i]));
        if (pn) {
            pn->x = 0; pn->y = 0;
            pn->width  = s->vertical ? cross : ext;
            pn->height = s->vertical ? ext   : cross;
            if (pn->layout) mel_gui__layout_arrange(pn->self);
        }

        used += ext + DIVIDER_THICK;
    }
}

static i32 hit_divider(Mel_Win32_Splitter* s, i32 axis_pos)
{
    i32 cursor = 0;
    for (i32 i = 0; i < s->pane_count - 1; i++) {
        cursor += s->sizes[i];
        if (axis_pos >= cursor && axis_pos < cursor + DIVIDER_THICK) return i;
        cursor += DIVIDER_THICK;
    }
    return -1;
}

static LRESULT CALLBACK splitter_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    Mel_Win32_Splitter* s = (Mel_Win32_Splitter*)mel_gui__win32_ctl(hwnd);

    switch (msg) {
        case WM_COMMAND:
            if (lp) { SendMessageW((HWND)lp, MEL_REFLECT(WM_COMMAND), wp, lp); return 0; }
            break;
        case WM_CTLCOLORSTATIC:
            SetBkMode((HDC)wp, TRANSPARENT);
            return (LRESULT)(UINT_PTR)GetSysColorBrush(COLOR_BTNFACE);
        case WM_SETCURSOR:
            if (s && LOWORD(lp) == HTCLIENT) {
                POINT pt; GetCursorPos(&pt); ScreenToClient(hwnd, &pt);
                i32 axis_pos = s->vertical ? pt.y : pt.x;
                if (hit_divider(s, axis_pos) >= 0) {
                    SetCursor(LoadCursorW(NULL, s->vertical ? (LPCWSTR)IDC_SIZENS : (LPCWSTR)IDC_SIZEWE));
                    return TRUE;
                }
            }
            break;
        case WM_LBUTTONDOWN:
            if (s) {
                i32 axis_pos = s->vertical ? GET_Y_LPARAM(lp) : GET_X_LPARAM(lp);
                i32 d = hit_divider(s, axis_pos);
                if (d >= 0) { s->dragging = true; s->drag_index = d; SetCapture(hwnd); }
            }
            return 0;
        case WM_MOUSEMOVE:
            if (s && s->dragging) {
                i32 d = s->drag_index;
                i32 axis_pos = s->vertical ? GET_Y_LPARAM(lp) : GET_X_LPARAM(lp);
                i32 start = 0;
                for (i32 i = 0; i < d; i++) start += s->sizes[i] + DIVIDER_THICK;
                i32 combined = s->sizes[d] + s->sizes[d + 1];
                i32 left = axis_pos - start;
                i32 min_l = s->mins[d];
                i32 min_r = s->mins[d + 1];
                if (left < min_l) left = min_l;
                if (left > combined - min_r) left = combined - min_r;
                s->sizes[d]     = left;
                s->sizes[d + 1] = combined - left;
                split_relayout(s);
            }
            return 0;
        case WM_LBUTTONUP:
            if (s && s->dragging) { s->dragging = false; ReleaseCapture(); }
            return 0;
        case WM_SIZE:
            split_relayout(s);
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

static void ensure_splitter_class(void)
{
    if (g_splitter_class) return;
    WNDCLASSEXW sc = {0};
    sc.cbSize        = sizeof sc;
    sc.style         = CS_HREDRAW | CS_VREDRAW;
    sc.lpfnWndProc   = splitter_wndproc;
    sc.hInstance     = current_hinst;
    sc.hCursor       = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    sc.hbrBackground = (HBRUSH)(UINT_PTR)(COLOR_BTNSHADOW + 1);
    sc.lpszClassName = SPLITTER_CLASS;
    RegisterClassExW(&sc);
    g_splitter_class = true;
}

static void split_arrange(Mel_Layout* layout, Mel_Gui_Handle container)
{
    (void)layout;
    Mel_Gui_Node* node = mel_gui__node(container);
    if (!node || !node->native) return;
    Mel_Win32_Splitter* s = (Mel_Win32_Splitter*)mel_gui__win32_ctl((HWND)node->native);
    split_relayout(s);
}

static const Mel_Layout_Vtable s_split_vtable = { .arrange = split_arrange };

Mel_Gui_Handle mel_splitter_create_opt(Mel_Gui_Handle parent, Mel_Splitter_Opt o)
{
    ensure_splitter_class();

    Mel_Layout* layout = (Mel_Layout*)mel_calloc(mel_gui__alloc(), sizeof *layout);
    if (layout) layout->vtable = &s_split_vtable;

    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    HWND par = mel_gui__win32_parent_hwnd(n);
    if (!par) return h;

    HWND hwnd = CreateWindowExW(0, SPLITTER_CLASS, NULL,
        mel_gui__win32_child_style(n, o.disabled) | WS_CLIPCHILDREN,
        n->x, n->y, n->width, n->height, par, NULL, current_hinst, NULL);
    n->native = hwnd;
    if (!hwnd) return h;

    Mel_Win32_Splitter* s = (Mel_Win32_Splitter*)mel_gui__win32_alloc_ctl(hwnd, sizeof *s, h);
    if (s) {
        s->base.focus = o.focus;
        s->vertical   = (o.orientation == MEL_SPLIT_VERTICAL);
        s->drag_index = -1;
    }
    return h;
}

Mel_Gui_Handle mel_splitpane_create_opt(Mel_Gui_Handle splitter, Mel_SplitPane_Opt o)
{
    Mel_Gui_Node* sn = mel_gui__node(splitter);
    if (!sn || !sn->native) return MEL_GUI_HANDLE_NONE;
    HWND swnd = (HWND)sn->native;
    Mel_Win32_Splitter* s = (Mel_Win32_Splitter*)mel_gui__win32_ctl(swnd);
    if (!s || s->pane_count >= MEL_SPLITTER_MAX_PANES) return MEL_GUI_HANDLE_NONE;

    Mel_Gui_Handle h = mel_gui__node_new(splitter, 0, 0, 0, 0, o.id, o.user, false,
                                         &o.layoutable, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    HWND pane = mel_gui__win32_make_container(swnd, 0, 0, 1, 1, h,
        (Mel_Gui_Pointer_Cb){0}, (Mel_Gui_Focus_Cb){0}, (Mel_Gui_Keyboard_Cb){0},
        false, false);

    n->native  = pane;
    n->content = pane;

    i32 idx = s->pane_count;
    s->panes[idx] = pane;
    s->sizes[idx] = o.initial_size > 0 ? o.initial_size : 0;
    s->mins[idx]  = o.min_size > 0 ? o.min_size : 0;
    s->pane_count++;

    split_relayout(s);
    return h;
}
