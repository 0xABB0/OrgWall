#include "../paint_internal.h"

#include <debug/assert.h>
#include <log/log.h>

#include <paint/painter.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static inline HDC      pdc(Mel_Painter* p) { return (HDC)p->native; }
static inline COLORREF cref(mel_color8 k) { return RGB(k.r, k.g, k.b); }
static inline int      ipx(f32 v) { return (int)(v + 0.5f); }

static HFONT g_font;
static int   g_font_px;

static HFONT painter_font(int px)
{
    if (g_font && g_font_px == px)
        return g_font;
    if (g_font)
        DeleteObject(g_font);
    g_font = CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    g_font_px = px;
    return g_font;
}

static int widen(str8 s, wchar_t* buf, int cap)
{
    if (!s.data || s.len == 0)
        return 0;
    int n = MultiByteToWideChar(CP_UTF8, 0, (const char*)s.data, (int)s.len, buf, cap);
    return n < 0 ? 0 : n;
}

Mel_Painter mel_painter_begin(Mel_Drawable dh)
{
    Paint_Drawable* d = mel_paint__get(dh);
    mel_assert(!d->painting);
    d->painting = true;
    SaveDC((HDC)d->native);
    return (Mel_Painter){ .native = d->native, .owner = dh, .w = d->w, .h = d->h };
}

void mel_painter_end(Mel_Painter* p)
{
    RestoreDC(pdc(p), -1);
    mel_paint__get(p->owner)->painting = false;
    p->native = NULL;
}

void mel_painter_clear(Mel_Painter* p, mel_color8 k)
{
    RECT rc = { 0, 0, p->w, p->h };
    SetDCBrushColor(pdc(p), cref(k));
    FillRect(pdc(p), &rc, (HBRUSH)GetStockObject(DC_BRUSH));
}

void mel_painter_fill_rect(Mel_Painter* p, Mel_Rect r, mel_color8 k)
{
    RECT rc = { ipx(r.x), ipx(r.y), ipx(r.x + r.w), ipx(r.y + r.h) };
    SetDCBrushColor(pdc(p), cref(k));
    FillRect(pdc(p), &rc, (HBRUSH)GetStockObject(DC_BRUSH));
}

void mel_painter_fill_ellipse(Mel_Painter* p, Mel_Rect r, mel_color8 k)
{
    HDC dc = pdc(p);
    SetDCBrushColor(dc, cref(k));
    HGDIOBJ ob = SelectObject(dc, GetStockObject(DC_BRUSH));
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, ipx(r.x), ipx(r.y), ipx(r.x + r.w), ipx(r.y + r.h));
    SelectObject(dc, ob);
    SelectObject(dc, op);
}

static HPEN begin_pen(HDC dc, mel_color8 k, f32 width, HGDIOBJ* old)
{
    int w = ipx(width);
    if (w < 1)
        w = 1;
    if (w <= 1)
    {
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
    if (pen)
        DeleteObject(pen);
}

void mel_painter_stroke_rect(Mel_Painter* p, Mel_Rect r, mel_color8 k, f32 width)
{
    HDC     dc = pdc(p);
    HGDIOBJ op;
    HPEN    pen = begin_pen(dc, k, width, &op);
    HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
    Rectangle(dc, ipx(r.x), ipx(r.y), ipx(r.x + r.w), ipx(r.y + r.h));
    SelectObject(dc, ob);
    end_pen(dc, pen, op);
}

void mel_painter_draw_line(Mel_Painter* p, Mel_Vec2 a, Mel_Vec2 b, mel_color8 k, f32 width)
{
    HDC     dc = pdc(p);
    HGDIOBJ op;
    HPEN    pen = begin_pen(dc, k, width, &op);
    MoveToEx(dc, ipx(a.x), ipx(a.y), NULL);
    LineTo(dc, ipx(b.x), ipx(b.y));
    end_pen(dc, pen, op);
}

void mel_painter_fill_round_rect(Mel_Painter* p, Mel_Rect r, f32 radius, mel_color8 k)
{
    HDC dc = pdc(p);
    SetDCBrushColor(dc, cref(k));
    HGDIOBJ ob = SelectObject(dc, GetStockObject(DC_BRUSH));
    HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
    int     d = ipx(radius * 2);
    RoundRect(dc, ipx(r.x), ipx(r.y), ipx(r.x + r.w), ipx(r.y + r.h), d, d);
    SelectObject(dc, ob);
    SelectObject(dc, op);
}

static Mel_Image        s_conv;
static const Mel_Alloc* s_conv_alloc;

static void v4_masks(BITMAPV4HEADER* h, bool rgba)
{
    h->bV4RedMask = rgba ? 0x000000FFu : 0x00FF0000u;
    h->bV4GreenMask = 0x0000FF00u;
    h->bV4BlueMask = rgba ? 0x00FF0000u : 0x000000FFu;
    h->bV4AlphaMask = 0xFF000000u;
}

void mel_painter_draw_image(Mel_Painter* p, const Mel_Image* img, Mel_Rect dst, const Mel_Alloc* scratch_alloc)
{
    mel_assert(p && img && img->format);

    const Mel_Image* src = img;
    Mel_Image_Plane  plane = mel_image_plane(src, 0);
    bool             tight = plane.stride == plane.w * 4;
    bool             rgba = img->format == &mel_image_rgba8;
    bool             direct = tight && (rgba || img->format == &mel_image_bgra8);

    if (!direct)
    {
        if (!scratch_alloc)
        {
            mel_log_fatal("paint", "mel_painter_draw_image: format '%s' needs conversion but no scratch allocator given", mel_image_format_name(img->format));
            MEL_BREAKPOINT();
            return;
        }
        if (s_conv.format && (s_conv.w != img->w || s_conv.h != img->h || s_conv_alloc != scratch_alloc))
        {
            mel_image_free(&s_conv);
            s_conv = (Mel_Image){ 0 };
        }
        if (!s_conv.format && !mel_image_init(&s_conv, &mel_image_bgra8, img->w, img->h, scratch_alloc))
        {
            mel_log_error("paint", "mel_painter_draw_image: scratch bgra8 init failed (%dx%d)", img->w, img->h);
            return;
        }
        s_conv_alloc = scratch_alloc;
        if (!mel_image_convert_scratch(img, &s_conv, scratch_alloc))
        {
            mel_log_error("paint", "mel_painter_draw_image: convert '%s' -> bgra8 failed", mel_image_format_name(img->format));
            return;
        }
        src = &s_conv;
        plane = mel_image_plane(src, 0);
        rgba = false;
    }

    BITMAPV4HEADER bi = { 0 };
    bi.bV4Size = sizeof bi;
    bi.bV4Width = plane.w;
    bi.bV4Height = -plane.h;
    bi.bV4Planes = 1;
    bi.bV4BitCount = 32;
    bi.bV4V4Compression = BI_BITFIELDS;
    v4_masks(&bi, rgba);

    HDC dc = pdc(p);
    int old_mode = SetStretchBltMode(dc, HALFTONE);
    SetBrushOrgEx(dc, 0, 0, NULL);
    StretchDIBits(dc, ipx(dst.x), ipx(dst.y), ipx(dst.w), ipx(dst.h), 0, 0, plane.w, plane.h, plane.pixels, (const BITMAPINFO*)&bi, DIB_RGB_COLORS, SRCCOPY);
    SetStretchBltMode(dc, old_mode);
}

void mel_painter_draw_text(Mel_Painter* p, str8 text, Mel_Vec2 pos, mel_color8 k, f32 size)
{
    HDC     dc = pdc(p);
    wchar_t wbuf[512];
    int     n = widen(text, wbuf, (int)(sizeof wbuf / sizeof wbuf[0]));

    HGDIOBJ  of = SelectObject(dc, painter_font(ipx(size)));
    int      obk = SetBkMode(dc, TRANSPARENT);
    COLORREF oc = SetTextColor(dc, cref(k));
    UINT     oalign = SetTextAlign(dc, TA_TOP | TA_LEFT);
    TextOutW(dc, ipx(pos.x), ipx(pos.y), wbuf, n);
    SetTextAlign(dc, oalign);
    SetTextColor(dc, oc);
    SetBkMode(dc, obk);
    SelectObject(dc, of);
}
