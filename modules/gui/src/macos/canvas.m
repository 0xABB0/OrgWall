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
    Mel_Gui_Widget* w = mel_gui__get(self.handle);
    if (w && w->cb && w->cb->pointer.on_pointer_enter) {
        w->cb->pointer.on_pointer_enter(self.handle, w->user);
    }
}

- (void)mouseExited:(NSEvent*)e
{
    (void)e;
    Mel_Gui_Widget* w = mel_gui__get(self.handle);
    if (w && w->cb && w->cb->pointer.on_pointer_leave) {
        w->cb->pointer.on_pointer_leave(self.handle, w->user);
    }
}

- (BOOL)becomeFirstResponder
{
    BOOL ok = [super becomeFirstResponder];
    if (ok) {
        mel_gui__set_focused(self.handle);
        mel_gui__macos_fire_focus_in(self.handle);
    }
    return ok;
}

- (BOOL)resignFirstResponder
{
    BOOL ok = [super resignFirstResponder];
    if (ok) {
        if (mel_gui_handle_eq(mel_gui_focused(), self.handle)) {
            mel_gui__set_focused(MEL_GUI_HANDLE_NONE);
        }
        mel_gui__macos_fire_focus_out(self.handle);
    }
    return ok;
}

- (void)drawRect:(NSRect)dirty
{
    (void)dirty;
    Mel_Gui_Widget* w = mel_gui__get(self.handle);
    if (!w) return;
    Mel_Gui_Canvas_Impl* impl = (Mel_Gui_Canvas_Impl*)w->impl;
    NSRect b = self.bounds;

    if (impl && impl->on_.on_paint) {
        CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
        impl->on_.on_paint(self.handle, ctx, (i32)b.size.width, (i32)b.size.height, w->user);
    } else {
        [[NSColor controlBackgroundColor] set];
        NSRectFill(b);
    }
}

- (NSPoint)pointFromEvent:(NSEvent*)e
{
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    return p;
}

- (void)mouseDown:(NSEvent*)e
{
    _pointer_down = true;
    NSPoint p = [self pointFromEvent:e];
    [self.window makeFirstResponder:self];
    mel_gui__fire_pointer_down(self.handle, (i32)p.x, (i32)p.y);
}

- (void)mouseDragged:(NSEvent*)e
{
    NSPoint p = [self pointFromEvent:e];
    mel_gui__fire_pointer_move(self.handle, (i32)p.x, (i32)p.y);
}

- (void)mouseMoved:(NSEvent*)e
{
    NSPoint p = [self pointFromEvent:e];
    mel_gui__fire_pointer_move(self.handle, (i32)p.x, (i32)p.y);
}

- (void)mouseUp:(NSEvent*)e
{
    NSPoint p = [self pointFromEvent:e];
    _pointer_down = false;
    mel_gui__fire_pointer_up(self.handle, (i32)p.x, (i32)p.y);
}

- (void)keyDown:(NSEvent*)e
{
    Mel_Key k = mel_gui__macos_key_for_event(e);
    mel_gui__fire_key_down(self.handle, k);

    NSString* chars = e.characters;
    for (NSUInteger i = 0; i < chars.length; i++) {
        unichar c = [chars characterAtIndex:i];
        mel_gui__fire_char(self.handle, (u32)c);
    }
}

- (void)keyUp:(NSEvent*)e
{
    Mel_Key k = mel_gui__macos_key_for_event(e);
    mel_gui__fire_key_up(self.handle, k);
}

@end

void mel_gui__backend_canvas_create(Mel_Gui_Widget* w, str8 text)
{
    (void)text;
    @autoreleasepool {
        MelGuiCanvasView* view = [[MelGuiCanvasView alloc] initWithFrame:NSMakeRect(0, 0, w->width, w->height)];
        view.handle = w->self;
        mel_gui__macos_install_child(w, view);
    }
}
