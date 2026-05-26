#include "macos.h"

#include <gui/painter.h>

static inline CGRect cg_rect(Mel_Rect r) { return CGRectMake(r.x, r.y, r.w, r.h); }

static inline void set_fill(CGContextRef c, Mel_Color k)
{
    CGContextSetRGBFillColor(c, k.r / 255.0, k.g / 255.0, k.b / 255.0, k.a / 255.0);
}

static inline void set_stroke(CGContextRef c, Mel_Color k)
{
    CGContextSetRGBStrokeColor(c, k.r / 255.0, k.g / 255.0, k.b / 255.0, k.a / 255.0);
}

void mel_painter_clear(Mel_Painter* p, Mel_Color k)
{
    set_fill(p->cg, k);
    CGContextFillRect(p->cg, CGRectMake(0, 0, p->w, p->h));
}

void mel_painter_fill_rect(Mel_Painter* p, Mel_Rect r, Mel_Color k)
{
    set_fill(p->cg, k);
    CGContextFillRect(p->cg, cg_rect(r));
}

void mel_painter_fill_ellipse(Mel_Painter* p, Mel_Rect r, Mel_Color k)
{
    set_fill(p->cg, k);
    CGContextFillEllipseInRect(p->cg, cg_rect(r));
}

void mel_painter_stroke_rect(Mel_Painter* p, Mel_Rect r, Mel_Color k, f32 width)
{
    set_stroke(p->cg, k);
    CGContextSetLineWidth(p->cg, width);
    CGContextStrokeRect(p->cg, cg_rect(r));
}

void mel_painter_draw_line(Mel_Painter* p, Mel_Vec2 a, Mel_Vec2 b, Mel_Color k, f32 width)
{
    set_stroke(p->cg, k);
    CGContextSetLineWidth(p->cg, width);
    CGContextSetLineCap(p->cg, kCGLineCapRound);
    CGContextMoveToPoint(p->cg, a.x, a.y);
    CGContextAddLineToPoint(p->cg, b.x, b.y);
    CGContextStrokePath(p->cg);
}

void mel_painter_fill_round_rect(Mel_Painter* p, Mel_Rect r, f32 radius, Mel_Color k)
{
    set_fill(p->cg, k);
    CGPathRef path = CGPathCreateWithRoundedRect(cg_rect(r), radius, radius, NULL);
    CGContextAddPath(p->cg, path);
    CGContextFillPath(p->cg);
    CGPathRelease(path);
}

void mel_painter_draw_text(Mel_Painter* p, str8 text, Mel_Vec2 pos, Mel_Color k, f32 size)
{
    (void)p;
    @autoreleasepool {
        NSString* s     = mel_gui__macos_nsstring(text);
        NSFont*   font  = [NSFont systemFontOfSize:size];
        NSColor*  color = [NSColor colorWithSRGBRed:k.r / 255.0
                                              green:k.g / 255.0
                                               blue:k.b / 255.0
                                              alpha:k.a / 255.0];
        [s drawAtPoint:NSMakePoint(pos.x, pos.y)
        withAttributes:@{ NSFontAttributeName: font, NSForegroundColorAttributeName: color }];
    }
}
