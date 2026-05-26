#include "uikit.h"

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
        NSString* s    = mel_gui__ios_nsstring(text);
        UIFont*   font = [UIFont systemFontOfSize:size];
        UIColor*  col  = [UIColor colorWithRed:k.r / 255.0 green:k.g / 255.0
                                          blue:k.b / 255.0 alpha:k.a / 255.0];
        [s drawAtPoint:CGPointMake(pos.x, pos.y)
        withAttributes:@{ NSFontAttributeName: font, NSForegroundColorAttributeName: col }];
    }
}

@implementation MelCanvas

- (void)drawRect:(CGRect)rect
{
    (void)rect;
    CGRect b = self.bounds;
    if (self.on_.on_paint) {
        CGContextRef ctx = UIGraphicsGetCurrentContext();
        struct Mel_Painter p = { .cg = ctx, .w = (f32)b.size.width, .h = (f32)b.size.height };
        self.on_.on_paint(self.handle, &p, (i32)b.size.width, (i32)b.size.height,
                          mel_gui_user(self.handle));
    }
}

- (void)touchesBegan:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    (void)event;
    UITouch* t = touches.anyObject;
    CGPoint  pt = [t locationInView:self];
    if (self.pointer.on_pointer_down)
        self.pointer.on_pointer_down(self.handle, (i32)pt.x, (i32)pt.y, mel_gui_user(self.handle));
}

- (void)touchesMoved:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    (void)event;
    UITouch* t = touches.anyObject;
    CGPoint  pt = [t locationInView:self];
    if (self.pointer.on_pointer_move)
        self.pointer.on_pointer_move(self.handle, (i32)pt.x, (i32)pt.y, mel_gui_user(self.handle));
}

- (void)touchesEnded:(NSSet<UITouch*>*)touches withEvent:(UIEvent*)event
{
    (void)event;
    UITouch* t = touches.anyObject;
    CGPoint  pt = [t locationInView:self];
    if (self.pointer.on_pointer_up)
        self.pointer.on_pointer_up(self.handle, (i32)pt.x, (i32)pt.y, mel_gui_user(self.handle));
    if (self.pointer.on_click)
        self.pointer.on_click(self.handle, mel_gui_user(self.handle));
}

@end

Mel_Gui_Handle mel_canvas_create_opt(Mel_Gui_Handle parent, Mel_Canvas_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    __block MelCanvas* view = nil;
    mel_gui__ios_sync(^{
        view = [[MelCanvas alloc] initWithFrame:CGRectMake(0, 0, n->width, n->height)];
        view.handle  = h;
        view.pointer = o.pointer;
        view.focus   = o.focus;
        view.on_     = o.on_;
        view.backgroundColor = [UIColor clearColor];
        view.contentMode     = UIViewContentModeRedraw;
    });
    mel_gui__ios_install_child(n, view);
    return h;
}
