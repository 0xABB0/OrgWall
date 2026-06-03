#include "uikit.h"

#import <QuartzCore/CAMetalLayer.h>

@implementation MelGpuView

+ (Class)layerClass
{
    return [CAMetalLayer class];
}

- (void)layoutSubviews
{
    [super layoutSubviews];
    CGSize sz = self.bounds.size;
    i32    w = (i32)sz.width, h = (i32)sz.height;
    if (w == self.last_w && h == self.last_h)
        return;
    self.last_w = w;
    self.last_h = h;
    if (self.on_.on_resize)
        self.on_.on_resize(self.handle, w, h, mel_gui_user(self.handle));
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
}

@end

Mel_Gui_Handle mel_gpu_view_create_opt(Mel_Gui_Handle parent, Mel_Gpu_View_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden, &o.layoutable, NULL);
    Mel_Gui_Node*  n = mel_gui__node(h);
    if (!n)
        return h;

    MelGpuView* view = [[MelGpuView alloc] initWithFrame:CGRectMake(0, 0, n->width, n->height)];
    view.handle = h;
    view.pointer = o.pointer;
    view.focus = o.focus;
    view.on_ = o.on_;
    view.last_w = 0;
    view.last_h = 0;
    view.opaque = YES;
    view.backgroundColor = [UIColor blackColor];
    mel_gui__ios_install_child(n, view);
    return h;
}

void* mel_gpu_view_surface(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    return n ? n->native : NULL;
}
