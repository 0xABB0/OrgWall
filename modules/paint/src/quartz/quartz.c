#include "../paint_internal.h"

#include <allocator/allocator.h>
#include <debug/assert.h>

#include <paint/painter.h>
#include <paint/pixmap.h>

#include <string.h>

#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>

static inline CGContextRef pcg(Mel_Painter* p) { return (CGContextRef)p->native; }

static inline CGRect cg_rect(Mel_Rect r) { return CGRectMake(r.x, r.y, r.w, r.h); }

static inline void set_fill(CGContextRef c, mel_color8 k) { CGContextSetRGBFillColor(c, k.r / 255.0, k.g / 255.0, k.b / 255.0, k.a / 255.0); }

static inline void set_stroke(CGContextRef c, mel_color8 k) { CGContextSetRGBStrokeColor(c, k.r / 255.0, k.g / 255.0, k.b / 255.0, k.a / 255.0); }

Mel_Pixmap mel_pixmap_create(const Mel_Alloc* alloc, i32 w, i32 h)
{
    mel_assert(w > 0 && h > 0);

    i32   stride = w * 4;
    usize size = (usize)stride * (usize)h;
    u8*   pixels = (u8*)mel_alloc(alloc, size);
    memset(pixels, 0, size);

    /* Device RGB, premultiplied, byte order R,G,B,A in memory — matches
     * mel_color8. Device (not sRGB) so fills land verbatim for readback. */
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef    cg = CGBitmapContextCreate(pixels, (size_t)w, (size_t)h, 8, (size_t)stride, cs, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(cs);

    /* Flip to y-down drawing with top-down memory (row 0 = top scanline). */
    CGContextTranslateCTM(cg, 0, h);
    CGContextScaleCTM(cg, 1, -1);

    Paint_Drawable rec = { .native = cg, .w = w, .h = h, .owns = true, .alloc = alloc, .pixels = pixels, .stride = stride, .painting = false };
    return mel_paint__insert(&rec);
}

void mel_pixmap_destroy(Mel_Pixmap pm)
{
    Paint_Drawable* d = mel_paint__get(pm);
    mel_assert(d->owns);
    mel_assert(!d->painting);
    CGContextRelease((CGContextRef)d->native);
    mel_dealloc(d->alloc, d->pixels);
    mel_paint__remove(pm);
}

Mel_Painter mel_painter_begin(Mel_Drawable dh)
{
    Paint_Drawable* d = mel_paint__get(dh);
    mel_assert(!d->painting);
    d->painting = true;
    CGContextSaveGState((CGContextRef)d->native);
    return (Mel_Painter){ .native = d->native, .owner = dh, .w = d->w, .h = d->h };
}

void mel_painter_end(Mel_Painter* p)
{
    CGContextRestoreGState(pcg(p));
    mel_paint__get(p->owner)->painting = false;
    p->native = NULL;
}

void mel_painter_clear(Mel_Painter* p, mel_color8 k)
{
    set_fill(pcg(p), k);
    CGContextFillRect(pcg(p), CGRectMake(0, 0, p->w, p->h));
}

void mel_painter_fill_rect(Mel_Painter* p, Mel_Rect r, mel_color8 k)
{
    set_fill(pcg(p), k);
    CGContextFillRect(pcg(p), cg_rect(r));
}

void mel_painter_fill_ellipse(Mel_Painter* p, Mel_Rect r, mel_color8 k)
{
    set_fill(pcg(p), k);
    CGContextFillEllipseInRect(pcg(p), cg_rect(r));
}

void mel_painter_stroke_rect(Mel_Painter* p, Mel_Rect r, mel_color8 k, f32 width)
{
    set_stroke(pcg(p), k);
    CGContextSetLineWidth(pcg(p), width);
    CGContextStrokeRect(pcg(p), cg_rect(r));
}

void mel_painter_draw_line(Mel_Painter* p, Mel_Vec2 a, Mel_Vec2 b, mel_color8 k, f32 width)
{
    CGContextRef cg = pcg(p);
    set_stroke(cg, k);
    CGContextSetLineWidth(cg, width);
    CGContextSetLineCap(cg, kCGLineCapRound);
    CGContextMoveToPoint(cg, a.x, a.y);
    CGContextAddLineToPoint(cg, b.x, b.y);
    CGContextStrokePath(cg);
}

void mel_painter_fill_round_rect(Mel_Painter* p, Mel_Rect r, f32 radius, mel_color8 k)
{
    CGContextRef cg = pcg(p);
    set_fill(cg, k);
    CGPathRef path = CGPathCreateWithRoundedRect(cg_rect(r), radius, radius, NULL);
    CGContextAddPath(cg, path);
    CGContextFillPath(cg);
    CGPathRelease(path);
}

void mel_painter_draw_text(Mel_Painter* p, str8 text, Mel_Vec2 pos, mel_color8 k, f32 size)
{
    CGContextRef cg = pcg(p);

    CFStringRef s = CFStringCreateWithBytes(NULL, text.data, (CFIndex)text.len, kCFStringEncodingUTF8, false);
    if (!s)
        return;

    CTFontRef       font = CTFontCreateWithName(CFSTR("Helvetica"), size, NULL);
    CGColorSpaceRef csp = CGColorSpaceCreateDeviceRGB();
    const CGFloat   comps[4] = { k.r / 255.0, k.g / 255.0, k.b / 255.0, k.a / 255.0 };
    CGColorRef      col = CGColorCreate(csp, comps);

    const void*           keys[] = { kCTFontAttributeName, kCTForegroundColorAttributeName };
    const void*           vals[] = { font, col };
    CFDictionaryRef       attrs = CFDictionaryCreate(NULL, keys, vals, 2, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFAttributedStringRef as = CFAttributedStringCreate(NULL, s, attrs);
    CTLineRef             line = CTLineCreateWithAttributedString(as);

    /* pos is top-left (y-down); CoreText draws from the baseline up. Place the
     * baseline at pos.y + ascent and locally cancel the context y-flip so glyphs
     * render upright rather than mirrored. */
    CGFloat ascent = CTFontGetAscent(font);
    CGContextSaveGState(cg);
    CGContextTranslateCTM(cg, pos.x, pos.y + ascent);
    CGContextScaleCTM(cg, 1, -1);
    CGContextSetTextPosition(cg, 0, 0);
    CTLineDraw(line, cg);
    CGContextRestoreGState(cg);

    CFRelease(line);
    CFRelease(as);
    CFRelease(attrs);
    CGColorRelease(col);
    CGColorSpaceRelease(csp);
    CFRelease(font);
    CFRelease(s);
}
