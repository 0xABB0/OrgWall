#include "win32.h"

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

static HFONT style_font(HWND hwnd, const Mel_Font* f)
{
    HFONT cur = (HFONT)SendMessageW(hwnd, WM_GETFONT, 0, 0);
    if (!cur)
        cur = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    LOGFONTW lf = { 0 };
    if (!GetObjectW(cur, sizeof lf, &lf))
        return NULL;
    if (f->size > 0)
    {
        HDC screen = GetDC(NULL);
        lf.lfHeight = -MulDiv((int)f->size, GetDeviceCaps(screen, LOGPIXELSY), 72);
        ReleaseDC(NULL, screen);
        lf.lfWidth = 0;
    }
    if (f->weight)
        lf.lfWeight = f->weight;
    if (f->italic)
        lf.lfItalic = TRUE;
    if (f->family.len)
        mel_gui__win32_widen(f->family, lf.lfFaceName, LF_FACESIZE);
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

static Mel_Win32_Ctl* style_target(Mel_Gui_Handle h, HWND* hwnd)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native)
        return NULL;
    *hwnd = (HWND)n->native;
    return style_ctl(*hwnd, h);
}

static void apply_font(HWND hwnd, Mel_Win32_Ctl* c, const Mel_Font* f)
{
    if (!mel_font_any(f))
        return;
    HFONT font = style_font(hwnd, f);
    if (!font)
        return;
    SendMessageW(hwnd, WM_SETFONT, (WPARAM)font, MAKELPARAM(TRUE, 0));
    if (c->font)
        DeleteObject(c->font);
    c->font = font;
}

static void apply_fg(HWND hwnd, Mel_Win32_Ctl* c, Mel_Style_Color fg)
{
    if (!fg.set)
        return;
    c->fg = RGB(fg.color.r, fg.color.g, fg.color.b);
    c->has_fg = true;
    InvalidateRect(hwnd, NULL, TRUE);
}

static void apply_bg(HWND hwnd, Mel_Win32_Ctl* c, Mel_Style_Color bg)
{
    if (!bg.set)
        return;
    style_store_bg(c, bg.color);
    InvalidateRect(hwnd, NULL, TRUE);
}

static void apply_corner(HWND hwnd, Mel_Win32_Ctl* c, f32 radius)
{
    if (radius <= 0)
        return;
    c->corner_radius = (i32)radius;
    style_apply_region(hwnd, c->corner_radius);
}

static void apply_surface(HWND hwnd, Mel_Win32_Ctl* c, const Mel_Style_Surface* s)
{
    apply_bg(hwnd, c, s->bg);
    apply_corner(hwnd, c, s->corner_radius);
}

/* A container whose visible surface is a separate content hwnd (scrollview
 * inner, groupbox inner) paints the bg there too. */
static void apply_content_bg(Mel_Gui_Node* n, Mel_Gui_Handle h, Mel_Style_Color bg)
{
    if (!bg.set || !n->content || n->content == n->native)
        return;
    Mel_Win32_Ctl* inner = style_ctl((HWND)n->content, h);
    if (!inner)
        return;
    style_store_bg(inner, bg.color);
    InvalidateRect((HWND)n->content, NULL, TRUE);
}

static void style_set_surface(Mel_Gui_Handle h, const Mel_Style_Surface* s)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native)
        return;
    HWND           hwnd = (HWND)n->native;
    Mel_Win32_Ctl* c = style_ctl(hwnd, h);
    if (!c)
        return;
    apply_surface(hwnd, c, s);
    apply_content_bg(n, h, s->bg);
}

void mel_label_set_style_opt(Mel_Gui_Handle h, Mel_Label_Style style)
{
    HWND           hwnd = NULL;
    Mel_Win32_Ctl* c = style_target(h, &hwnd);
    if (!c)
        return;
    apply_font(hwnd, c, &style.font);
    apply_fg(hwnd, c, style.fg);
    apply_surface(hwnd, c, &style.surface);
}

void mel_button_set_style_opt(Mel_Gui_Handle h, Mel_Button_Style style)
{
    HWND           hwnd = NULL;
    Mel_Win32_Ctl* c = style_target(h, &hwnd);
    if (!c)
        return;
    apply_font(hwnd, c, &style.font);
    apply_corner(hwnd, c, style.surface.corner_radius);
}

void mel_checkbox_set_style_opt(Mel_Gui_Handle h, Mel_CheckBox_Style style)
{
    HWND           hwnd = NULL;
    Mel_Win32_Ctl* c = style_target(h, &hwnd);
    if (!c)
        return;
    apply_font(hwnd, c, &style.font);
    apply_fg(hwnd, c, style.fg);
    apply_surface(hwnd, c, &style.surface);
}

void mel_textfield_set_style_opt(Mel_Gui_Handle h, Mel_TextField_Style style)
{
    HWND           hwnd = NULL;
    Mel_Win32_Ctl* c = style_target(h, &hwnd);
    if (!c)
        return;
    apply_font(hwnd, c, &style.font);
    apply_fg(hwnd, c, style.fg);
    apply_surface(hwnd, c, &style.surface);
}

/* The trackbar owner-draws thumb and channel: track and thumb are not honestly
 * reachable, only the surface applies. */
void mel_slider_set_style_opt(Mel_Gui_Handle h, Mel_Slider_Style style)
{
    HWND           hwnd = NULL;
    Mel_Win32_Ctl* c = style_target(h, &hwnd);
    if (!c)
        return;
    apply_surface(hwnd, c, &style.surface);
}

void mel_groupbox_set_style_opt(Mel_Gui_Handle h, Mel_GroupBox_Style style)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native)
        return;
    HWND           hwnd = (HWND)n->native;
    Mel_Win32_Ctl* c = style_ctl(hwnd, h);
    if (!c)
        return;
    apply_font(hwnd, c, &style.title_font);
    apply_fg(hwnd, c, style.title_fg);
    apply_surface(hwnd, c, &style.surface);
    apply_content_bg(n, h, style.surface.bg);
}

void mel_splitter_set_style_opt(Mel_Gui_Handle h, Mel_Splitter_Style style)
{
    HWND           hwnd = NULL;
    Mel_Win32_Ctl* c = style_target(h, &hwnd);
    if (!c)
        return;
    apply_surface(hwnd, c, &style.surface);
    apply_bg(hwnd, c, style.divider);
}

void mel_splitpane_set_style_opt(Mel_Gui_Handle h, Mel_SplitPane_Style style) { style_set_surface(h, &style.surface); }

void mel_panel_set_style_opt(Mel_Gui_Handle h, Mel_Panel_Style style) { style_set_surface(h, &style.surface); }

void mel_canvas_set_style_opt(Mel_Gui_Handle h, Mel_Canvas_Style style) { style_set_surface(h, &style.surface); }

void mel_scrollview_set_style_opt(Mel_Gui_Handle h, Mel_ScrollView_Style style) { style_set_surface(h, &style.surface); }

void mel_frame_set_style_opt(Mel_Gui_Handle h, Mel_Frame_Style style) { style_set_surface(h, &style.surface); }

void mel_dialog_set_style_opt(Mel_Gui_Handle h, Mel_Dialog_Style style) { style_set_surface(h, &style.surface); }

void mel_tabview_set_style_opt(Mel_Gui_Handle h, Mel_TabView_Style style) { style_set_surface(h, &style.surface); }

void mel_tab_set_style_opt(Mel_Gui_Handle h, Mel_Tab_Style style) { style_set_surface(h, &style.surface); }
