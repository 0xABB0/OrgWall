#include "../paint_internal.h"

#include <core/compiler.h>

#include <debug/assert.h>
#include <log/log.h>

#include <paint/painter.h>
#include <paint/pixmap.h>

#include <CoreGraphics/CoreGraphics.h>
#include <CoreText/CoreText.h>

static CGColorSpaceRef cs_rgb(void);
static CGColorSpaceRef cs_gray(void);

static inline CGContextRef pcg(Mel_Painter* p) { return (CGContextRef)p->native; }

static inline CGRect cg_rect(Mel_Rect r) { return CGRectMake(r.x, r.y, r.w, r.h); }

static inline void set_fill(CGContextRef c, mel_color8 k) { CGContextSetRGBFillColor(c, k.r / 255.0, k.g / 255.0, k.b / 255.0, k.a / 255.0); }

static inline void set_stroke(CGContextRef c, mel_color8 k) { CGContextSetRGBStrokeColor(c, k.r / 255.0, k.g / 255.0, k.b / 255.0, k.a / 255.0); }

Mel_Pixmap mel_pixmap_create(const Mel_Alloc* alloc, i32 w, i32 h)
{
    mel_assert(w > 0 && h > 0);

    Mel_Image img;
    if (!mel_image_init(&img, &mel_image_rgba8_premul, w, h, alloc))
    {
        mel_log_fatal("paint", "mel_pixmap_create: image init failed (%dx%d)", w, h);
        MEL_BREAKPOINT();
    }

    Mel_Image_Plane plane = mel_image_plane(&img, 0);

    CGContextRef cg = CGBitmapContextCreate(plane.pixels, (size_t)w, (size_t)h, 8, (size_t)plane.stride, cs_rgb(), kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);

    CGContextTranslateCTM(cg, 0, h);
    CGContextScaleCTM(cg, 1, -1);

    Paint_Drawable rec = { .native = cg, .w = w, .h = h, .owns = true, .img = img, .painting = false };
    return mel_paint__insert(&rec);
}

void mel_pixmap_destroy(Mel_Pixmap pm)
{
    Paint_Drawable* d = mel_paint__get(pm);
    mel_assert(d->owns);
    mel_assert(!d->painting);
    CGContextRelease((CGContextRef)d->native);
    mel_image_free(&d->img);
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

static CGImageRef cg_image_from_plane(Mel_Image_Plane plane, CGColorSpaceRef cs, CGBitmapInfo info)
{
    CGDataProviderRef prov = CGDataProviderCreateWithData(NULL, plane.pixels, (size_t)plane.stride * (size_t)plane.h, NULL);
    if (!prov)
        return NULL;
    i32        bpc = 8;
    i32        bpp = plane.bpp * 8;
    CGImageRef img = CGImageCreate((size_t)plane.w, (size_t)plane.h, (size_t)bpc, (size_t)bpp, (size_t)plane.stride, cs, info, prov, NULL, false, kCGRenderingIntentDefault);
    CGDataProviderRelease(prov);
    return img;
}

static CGColorSpaceRef cs_rgb(void)
{
    static CGColorSpaceRef cs;
    if (!cs)
        cs = CGColorSpaceCreateDeviceRGB();
    return cs;
}

static CGColorSpaceRef cs_gray(void)
{
    static CGColorSpaceRef cs;
    if (!cs)
        cs = CGColorSpaceCreateDeviceGray();
    return cs;
}

void mel_painter_draw_image(Mel_Painter* p, const Mel_Image* img, Mel_Rect dst, const Mel_Alloc* scratch_alloc)
{
    mel_assert(p && img && img->format);

    const mel_image_format* fmt = img->format;

    Mel_Image        scratch = { 0 };
    const Mel_Image* src = img;

    bool is_rgba8 = (fmt == &mel_image_rgba8);
    bool is_rgba8_premul = (fmt == &mel_image_rgba8_premul);
    bool is_gray8 = (fmt == &mel_image_gray8);
    bool is_bgra8 = (fmt == &mel_image_bgra8);

    if (!is_rgba8 && !is_rgba8_premul && !is_gray8 && !is_bgra8)
    {
        if (!scratch_alloc)
        {
            mel_log_fatal("paint", "mel_painter_draw_image: format '%s' needs conversion but no scratch allocator given", mel_image_format_name(fmt));
            MEL_BREAKPOINT();
            return;
        }
        if (!mel_image_init(&scratch, &mel_image_rgba8, img->w, img->h, scratch_alloc))
        {
            mel_log_fatal("paint", "mel_painter_draw_image: scratch rgba8 init failed (%dx%d)", img->w, img->h);
            MEL_BREAKPOINT();
            return;
        }
        if (!mel_image_convert_scratch(img, &scratch, scratch_alloc))
        {
            mel_log_fatal("paint", "mel_painter_draw_image: convert '%s' -> rgba8 failed", mel_image_format_name(fmt));
            MEL_BREAKPOINT();
            mel_image_free(&scratch);
            return;
        }
        src = &scratch;
        is_rgba8 = true;
    }

    Mel_Image_Plane plane = mel_image_plane(src, 0);

    CGColorSpaceRef cs;
    CGBitmapInfo    info;
    if (is_gray8)
    {
        cs = cs_gray();
        info = kCGImageAlphaNone;
    }
    else if (is_bgra8)
    {
        cs = cs_rgb();
        info = (CGBitmapInfo)kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little;
    }
    else
    {
        cs = cs_rgb();
        info = (CGBitmapInfo)(is_rgba8_premul ? kCGImageAlphaPremultipliedLast : kCGImageAlphaLast) | kCGBitmapByteOrder32Big;
    }

    CGImageRef cgimg = cg_image_from_plane(plane, cs, info);

    if (!cgimg)
    {
        mel_log_error("paint", "mel_painter_draw_image: CGImageCreate failed");
        if (scratch.format)
            mel_image_free(&scratch);
        return;
    }

    CGContextRef cg = pcg(p);
    CGContextSaveGState(cg);
    CGContextTranslateCTM(cg, dst.x, dst.y + dst.h);
    CGContextScaleCTM(cg, 1, -1);
    CGContextDrawImage(cg, CGRectMake(0, 0, dst.w, dst.h), cgimg);
    CGContextRestoreGState(cg);

    CGImageRelease(cgimg);
    if (scratch.format)
        mel_image_free(&scratch);
}

void mel_painter_draw_text(Mel_Painter* p, str8 text, Mel_Vec2 pos, mel_color8 k, f32 size)
{
    CGContextRef cg = pcg(p);

    CFStringRef s = CFStringCreateWithBytes(NULL, text.data, (CFIndex)text.len, kCFStringEncodingUTF8, false);
    if (!s)
        return;

    CTFontRef     font = CTFontCreateWithName(CFSTR("Helvetica"), size, NULL);
    const CGFloat comps[4] = { k.r / 255.0, k.g / 255.0, k.b / 255.0, k.a / 255.0 };
    CGColorRef    col = CGColorCreate(cs_rgb(), comps);

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
    CFRelease(font);
    CFRelease(s);
}
