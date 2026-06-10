#include "uikit.h"

#include <paint/paint.h>

@implementation MelCanvas

- (void)drawRect:(CGRect)rect
{
    (void)rect;
    CGRect b = self.bounds;
    if (self.on_.on_paint)
    {
        CGContextRef ctx = UIGraphicsGetCurrentContext();
        Mel_Drawable d = mel_drawable_borrow(ctx, (i32)b.size.width, (i32)b.size.height);
        Mel_Painter  p = mel_painter_begin(d);
        self.on_.on_paint(self.handle, &p, (i32)b.size.width, (i32)b.size.height, mel_gui_user(self.handle));
        mel_painter_end(&p);
        mel_drawable_release(d);
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
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden, &o.layoutable, NULL);
    Mel_Gui_Node*  n = mel_gui__node(h);
    if (!n)
        return h;

    MelCanvas* view = [[MelCanvas alloc] initWithFrame:CGRectMake(0, 0, n->width, n->height)];
    view.handle = h;
    view.pointer = o.pointer;
    view.focus = o.focus;
    view.on_ = o.on_;
    view.backgroundColor = [UIColor clearColor];
    view.contentMode = UIViewContentModeRedraw;
    mel_gui__ios_install_child(n, view);
    if (mel_canvas_style_any(&o.style))
        mel_canvas_set_style_opt(h, o.style);
    return h;
}
