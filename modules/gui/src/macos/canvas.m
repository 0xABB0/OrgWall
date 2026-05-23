#include "macos.h"

@implementation MelGuiCanvasView {
    bool _pointer_down;
}

- (BOOL)isFlipped              { return YES; }
- (BOOL)acceptsFirstResponder  { return YES; }
- (BOOL)canBecomeKeyView       { return YES; }
- (BOOL)isOpaque               { return YES; }

- (void)updateTrackingAreas
{
    [super updateTrackingAreas];
    for (NSTrackingArea* ta in self.trackingAreas) {
        [self removeTrackingArea:ta];
    }
    NSTrackingAreaOptions opts = NSTrackingMouseMoved
                               | NSTrackingMouseEnteredAndExited
                               | NSTrackingActiveInActiveApp
                               | NSTrackingInVisibleRect;
    NSTrackingArea* ta = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                                      options:opts
                                                        owner:self
                                                     userInfo:nil];
    [self addTrackingArea:ta];
}

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

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    NSRect b = self.bounds;
    if (self.on_.on_paint) {
        CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
        struct Mel_Painter p = { .cg = ctx, .w = (f32)b.size.width, .h = (f32)b.size.height };
        self.on_.on_paint(self.handle, &p, (i32)b.size.width, (i32)b.size.height, mel_gui_user(self.handle));
    } else {
        [[NSColor controlBackgroundColor] set];
        NSRectFill(b);
    }
}

- (NSPoint)pointFromEvent:(NSEvent*)e
{
    return [self convertPoint:e.locationInWindow fromView:nil];
}

- (void)mouseDown:(NSEvent*)e
{
    _pointer_down = true;
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
    _pointer_down = false;
    if (self.pointer.on_pointer_up) self.pointer.on_pointer_up(self.handle, (i32)p.x, (i32)p.y, mel_gui_user(self.handle));
}

- (void)keyDown:(NSEvent*)e
{
    mel_gui__macos_key(self.handle, self.keyboard, e, true);
    if (self.keyboard.on_char) {
        NSString* chars = e.characters;
        for (NSUInteger i = 0; i < chars.length; i++) {
            self.keyboard.on_char(self.handle, (u32)[chars characterAtIndex:i], mel_gui_user(self.handle));
        }
    }
}

- (void)keyUp:(NSEvent*)e
{
    mel_gui__macos_key(self.handle, self.keyboard, e, false);
}

@end

Mel_Gui_Handle mel_canvas_create_opt(Mel_Gui_Handle parent, Mel_Canvas_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    @autoreleasepool {
        MelGuiCanvasView* view = [[MelGuiCanvasView alloc] initWithFrame:NSMakeRect(0, 0, n->width, n->height)];
        view.handle   = h;
        view.pointer  = o.pointer;
        view.focus    = o.focus;
        view.keyboard = o.keyboard;
        view.on_      = o.on_;
        mel_gui__macos_install_child(n, view);
    }
    return h;
}
