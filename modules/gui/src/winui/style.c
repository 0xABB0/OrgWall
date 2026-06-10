#include "win32.h"

static bool style_wants_font(const Mel_Style* s) { return s->font_family.len || s->font_size || s->font_weight || s->italic; }

static bool style_is_trackbar(HWND hwnd)
{
    wchar_t cls[64];
    GetClassNameW(hwnd, cls, 64);
    return lstrcmpiW(cls, TRACKBAR_CLASSW) == 0;
}

static void style_apply_region(HWND hwnd, i32 radius)
{
    RECT rc;
    GetWindowRect(hwnd, &rc);
    /* SetWindowRgn owns the region; +1 because the region right/bottom edge is exclusive. */
    HRGN rgn = CreateRoundRectRgn(0, 0, (rc.right - rc.left) + 1, (rc.bottom - rc.top) + 1, radius * 2, radius * 2);
    SetWindowRgn(hwnd, rgn, TRUE);
}

/* Installed on every styled window: paints the bg under WM_ERASEBKGND,
 * re-applies the rounded region after a resize, and frees the ctl of windows
 * that were created ctl-less (STATIC, BS_GROUPBOX, the tab control). It runs
 * ahead of the class proc; mel_gui__win32_free_ctl is idempotent next to the
 * per-widget subclasses and wndprocs that also free at WM_NCDESTROY. */
static LRESULT CALLBACK style_subclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR ref)
{
    (void)ref;
    Mel_Win32_Ctl* c = mel_gui__win32_ctl(hwnd);
    if (msg == WM_ERASEBKGND && c && c->has_bg && c->bg_brush)
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect((HDC)wp, &rc, c->bg_brush);
        return 1;
    }
    if (msg == WM_WINDOWPOSCHANGED && c && c->corner_radius > 0 && !(((WINDOWPOS*)lp)->flags & SWP_NOSIZE))
    {
        style_apply_region(hwnd, c->corner_radius);
    }
    if (msg == WM_NCDESTROY)
    {
        RemoveWindowSubclass(hwnd, style_subclass, id);
        mel_gui__win32_free_ctl(hwnd);
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}

static Mel_Win32_Ctl* style_ctl(HWND hwnd, Mel_Gui_Handle h)
{
    Mel_Win32_Ctl* c = mel_gui__win32_ctl(hwnd);
    if (!c)
        c = (Mel_Win32_Ctl*)mel_gui__win32_alloc_ctl(hwnd, sizeof *c, h);
    if (c)
        SetWindowSubclass(hwnd, style_subclass, 1, 0);
    return c;
}

static HFONT style_font(HWND hwnd, const Mel_Style* s)
{
    HFONT cur = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
    if (!cur)
        cur = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    LOGFONTW lf = { 0 };
    if (!GetObjectW(cur, sizeof lf, &lf))
        return NULL;
    if (s->font_size > 0)
    {
        HDC screen = GetDC(NULL);
        lf.lfHeight = -MulDiv((int)s->font_size, GetDeviceCaps(screen, LOGPIXELSY), 72);
        ReleaseDC(NULL, screen);
        lf.lfWidth = 0;
    }
    if (s->font_weight)
        lf.lfWeight = s->font_weight;
    if (s->italic)
        lf.lfItalic = TRUE;
    if (s->font_family.len)
        mel_gui__win32_widen(s->font_family, lf.lfFaceName, LF_FACESIZE);
    lf.lfQuality = CLEARTYPE_QUALITY;
    return CreateFontIndirectW(&lf);
}

static void style_store_bg(Mel_Win32_Ctl* c, mel_color8 col)
{
    c->bg = RGB(col.r, col.g, col.b);
    if (c->bg_brush)
        DeleteObject(c->bg_brush);
    c->bg_brush = CreateSolidBrush(c->bg);
    c->has_bg = true;
}

LRESULT mel_gui__win32_ctl_color(UINT msg, HDC dc, HWND container, HWND child)
{
    Mel_Win32_Ctl* c = mel_gui__win32_ctl(child);
    Mel_Win32_Ctl* pc = mel_gui__win32_ctl(container);
    bool           has_fg = c && c->has_fg;
    bool           has_bg = c && c->has_bg;
    bool           parent_bg = pc && pc->has_bg;
    if (!has_fg && !has_bg && !parent_bg)
        return 0;
    if (has_fg)
        SetTextColor(dc, c->fg);
    if (has_bg)
    {
        SetBkColor(dc, c->bg);
        return (LRESULT)(UINT_PTR)c->bg_brush;
    }
    if (msg == WM_CTLCOLOREDIT)
    {
        /* fg-only edit keeps its native surface under the recolored text. */
        if (!has_fg)
            return 0;
        SetBkColor(dc, GetSysColor(COLOR_WINDOW));
        return (LRESULT)(UINT_PTR)GetSysColorBrush(COLOR_WINDOW);
    }
    SetBkMode(dc, TRANSPARENT);
    if (parent_bg)
        return (LRESULT)(UINT_PTR)pc->bg_brush;
    return (LRESULT)(UINT_PTR)GetSysColorBrush(COLOR_BTNFACE);
}

void mel_gui_set_style(Mel_Gui_Handle h, Mel_Style style)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native)
        return;
    HWND           hwnd = (HWND)n->native;
    Mel_Win32_Ctl* c = style_ctl(hwnd, h);
    if (!c)
        return;

    if (style_wants_font(&style))
    {
        HFONT font = style_font(hwnd, &style);
        if (font)
        {
            SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, MAKELPARAM(TRUE, 0));
            if (c->font)
                DeleteObject(c->font);
            c->font = font;
        }
    }

    /* The trackbar owner-draws thumb and channel; its colors are not honestly
     * reachable, only the font path applies. */
    bool colorable = !style_is_trackbar(hwnd);
    if (colorable && style.fg.set)
    {
        c->fg = RGB(style.fg.color.r, style.fg.color.g, style.fg.color.b);
        c->has_fg = true;
    }
    if (colorable && style.bg.set)
    {
        style_store_bg(c, style.bg.color);
        /* A container whose visible surface is a separate content hwnd
         * (scrollview inner, groupbox inner) paints the bg there too. */
        if (n->content && n->content != n->native)
        {
            Mel_Win32_Ctl* inner = style_ctl((HWND)n->content, h);
            if (inner)
            {
                style_store_bg(inner, style.bg.color);
                InvalidateRect((HWND)n->content, NULL, TRUE);
            }
        }
    }
    if (colorable && (style.fg.set || style.bg.set))
        InvalidateRect(hwnd, NULL, TRUE);

    if (style.corner_radius > 0)
    {
        c->corner_radius = (i32)style.corner_radius;
        style_apply_region(hwnd, c->corner_radius);
    }
}
