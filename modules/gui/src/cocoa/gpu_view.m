#include "macos.h"

@implementation MelGuiGpuView

- (BOOL)isFlipped              { return YES; }
- (BOOL)acceptsFirstResponder  { return YES; }
- (BOOL)canBecomeKeyView       { return YES; }
- (BOOL)isOpaque               { return YES; }

- (void)setFrameSize:(NSSize)newSize
{
    [super setFrameSize:newSize];
    if (self.on_.on_resize) {
        self.on_.on_resize(self.handle, (i32)newSize.width, (i32)newSize.height,
                           mel_gui_user(self.handle));
    }
}

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    for (NSTrackingArea* ta in self.trackingAreas) [self removeTrackingArea:ta];
    NSTrackingAreaOptions opts = NSTrackingMouseMoved
                               | NSTrackingMouseEnteredAndExited
                               | NSTrackingActiveInActiveApp
                               | NSTrackingInVisibleRect;
    NSTrackingArea* ta = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                                      options:opts owner:self userInfo:nil];
    [self addTrackingArea:ta];
}

- (NSPoint)pointFromEvent:(NSEvent*)e { return [self convertPoint:e.locationInWindow fromView:nil]; }

- (void)mouseEntered:(NSEvent*)e
{
    (void)e;
    if (self.pointer.on_pointer_enter) self.pointer.on_pointer_enter(self.handle, mel_gui_user(self.handle));
}
- (void)mouseExited:(NSEvent*)e
{
    (void)e;
    if (self.pointer.on_pointer_leave) self.pointer.on_pointer_leave(self.handle, mel_gui_user(self.handle));
}
- (void)mouseDown:(NSEvent*)e
{
    NSPoint p = [self pointFromEvent:e];
    [self.window makeFirstResponder:self];
    if (self.pointer.on_pointer_down) self.pointer.on_pointer_down(self.handle, (i32)p.x, (i32)p.y, mel_gui_user(self.handle));
}
- (void)mouseDragged:(NSEvent*)e
{
    NSPoint p = [self pointFromEvent:e];
    if (self.pointer.on_pointer_move) self.pointer.on_pointer_move(self.handle, (i32)p.x, (i32)p.y, mel_gui_user(self.handle));
}
- (void)mouseMoved:(NSEvent*)e
{
    NSPoint p = [self pointFromEvent:e];
    if (self.pointer.on_pointer_move) self.pointer.on_pointer_move(self.handle, (i32)p.x, (i32)p.y, mel_gui_user(self.handle));
}
- (void)mouseUp:(NSEvent*)e
{
    NSPoint p = [self pointFromEvent:e];
    if (self.pointer.on_pointer_up) self.pointer.on_pointer_up(self.handle, (i32)p.x, (i32)p.y, mel_gui_user(self.handle));
}
- (void)keyDown:(NSEvent*)e { mel_gui__macos_key(self.handle, self.keyboard, e, true); }
- (void)keyUp:(NSEvent*)e   { mel_gui__macos_key(self.handle, self.keyboard, e, false); }

- (BOOL)becomeFirstResponder
{
    BOOL ok = [super becomeFirstResponder];
    if (ok) mel_gui__macos_focus_in(self.handle, self.focus);
    return ok;
}
- (BOOL)resignFirstResponder
{
    BOOL ok = [super resignFirstResponder];
    if (ok) mel_gui__macos_focus_out(self.handle, self.focus);
    return ok;
}

@end

Mel_Gui_Handle mel_gpu_view_create_opt(Mel_Gui_Handle parent, Mel_Gpu_View_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    @autoreleasepool {
        MelGuiGpuView* view = [[MelGuiGpuView alloc] initWithFrame:NSMakeRect(0, 0, n->width, n->height)];
        view.handle   = h;
        view.pointer  = o.pointer;
        view.focus    = o.focus;
        view.keyboard = o.keyboard;
        view.on_      = o.on_;
        mel_gui__macos_install_child(n, view);
    }
    return h;
}

void* mel_gpu_view_surface(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    return n ? n->native : NULL;
}
