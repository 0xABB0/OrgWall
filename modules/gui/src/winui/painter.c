#include "win32.h"

#include <gui/painter.h>

static inline COLORREF cref(Mel_Color k) { return RGB(k.r, k.g, k.b); }
static inline int       ipx (f32 v)       { return (int)(v + 0.5f); }

/* One-entry font cache keyed by pixel height: a paint pass that draws many
 * same-size strings reuses one HFONT instead of churning a GDI object each
 * call. */
static HFONT g_font;
static int   g_font_px;

static HFONT painter_font(int px)
{
    if (g_font && g_font_px == px) return g_font;
    if (g_font) DeleteObject(g_font);
    g_font = CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                         DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_font_px = px;
    return g_font;
}

void mel_painter_clear(Mel_Painter* p, Mel_Color k)
{
    RECT rc = { 0, 0, p->w, p->h };
    SetDCBrushColor(p->dc, cref(k));
    FillRect(p->dc, &rc, (HBRUSH)GetStockObject(DC_BRUSH));
}

void mel_painter_fill_rect(Mel_Painter* p, Mel_Rect r, Mel_Color k)
{
    RECT rc = { ipx(r.x), ipx(r.y), ipx(r.x + r.w), ipx(r.y + r.h) };
    SetDCBrushColor(p->dc, cref(k));
    FillRect(p->dc, &rc, (HBRUSH)GetStockObject(DC_BRUSH));
}

void mel_painter_fill_ellipse(Mel_Painter* p, Mel_Rect r, Mel_Color k)
{
    HDC dc = p->dc;
    SetDCBrushColor(dc, cref(k));
    HGDIOBJ ob = SelectObject(dc, GetStockObject(DC_BRUSH));
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, ipx(r.x), ipx(r.y), ipx(r.x + r.w), ipx(r.y + r.h));
    SelectObject(dc, ob);
    SelectObject(dc, op);
}

/* DC_PEN (a stock pen recoloured via SetDCPenColor) covers the common 1px
 * stroke with no allocation; only a wider pen needs CreatePen. */
static HPEN begin_pen(HDC dc, Mel_Color k, f32 width, HGDIOBJ* old)
{
    int w = ipx(width);
    if (w < 1) w = 1;
    if (w <= 1) {
        SetDCPenColor(dc, cref(k));
        *old = SelectObject(dc, GetStockObject(DC_PEN));
        return NULL;
    }
    HPEN pen = CreatePen(PS_SOLID, w, cref(k));
    *old = SelectObject(dc, pen);
    return pen;
}

static void end_pen(HDC dc, HPEN pen, HGDIOBJ old)
{
    SelectObject(dc, old);
    if (pen) DeleteObject(pen);
}

void mel_painter_stroke_rect(Mel_Painter* p, Mel_Rect r, Mel_Color k, f32 width)
{
    HDC dc = p->dc;
    HGDIOBJ op;
    HPEN    pen = begin_pen(dc, k, width, &op);
    HGDIOBJ ob  = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, ipx(r.x), ipx(r.y), ipx(r.x + r.w), ipx(r.y + r.h));
    SelectObject(dc, ob);
    end_pen(dc, pen, op);
}

void mel_painter_draw_line(Mel_Painter* p, Mel_Vec2 a, Mel_Vec2 b, Mel_Color k, f32 width)
{
    HDC dc = p->dc;
    HGDIOBJ op;
    HPEN    pen = begin_pen(dc, k, width, &op);
    MoveToEx(dc, ipx(a.x), ipx(a.y), NULL);
    LineTo(dc, ipx(b.x), ipx(b.y));
    end_pen(dc, pen, op);
}

void mel_painter_fill_round_rect(Mel_Painter* p, Mel_Rect r, f32 radius, Mel_Color k)
{
    HDC dc = p->dc;
    SetDCBrushColor(dc, cref(k));
    HGDIOBJ ob = SelectObject(dc, GetStockObject(DC_BRUSH));
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    int d = ipx(radius * 2);
    RoundRect(dc, ipx(r.x), ipx(r.y), ipx(r.x + r.w), ipx(r.y + r.h), d, d);
    SelectObject(dc, ob);
    SelectObject(dc, op);
}

void mel_painter_draw_text(Mel_Painter* p, str8 text, Mel_Vec2 pos, Mel_Color k, f32 size)
{
    HDC     dc = p->dc;
    wchar_t wbuf[512];
    int     n = mel_gui__win32_widen(text, wbuf, (int)(sizeof wbuf / sizeof wbuf[0]));

    HGDIOBJ  of    = SelectObject(dc, painter_font(ipx(size)));
    int      obk   = SetBkMode(dc, TRANSPARENT);
    COLORREF oc    = SetTextColor(dc, cref(k));
    UINT     oalign = SetTextAlign(dc, TA_TOP | TA_LEFT);
    TextOutW(dc, ipx(pos.x), ipx(pos.y), wbuf, n);
    SetTextAlign(dc, oalign);
    SetTextColor(dc, oc);
    SetBkMode(dc, obk);
    SelectObject(dc, of);
}
