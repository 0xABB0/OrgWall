#include "macos.h"

@implementation MelGuiContainerView

- (BOOL)isFlipped             { return YES; }
- (BOOL)acceptsFirstResponder { return self.focus.on_focus_in || self.focus.on_focus_out; }

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

- (NSPoint)pointFromEvent:(NSEvent*)e { return [self convertPoint:e.locationInWindow fromView:nil]; }

- (void)mouseDown:(NSEvent*)e
{
    NSPoint p = [self pointFromEvent:e];
    if (self.pointer.on_pointer_down) self.pointer.on_pointer_down(self.handle, (i32)p.x, (i32)p.y, mel_gui_user(self.handle));
}

- (void)mouseUp:(NSEvent*)e
{
    NSPoint p = [self pointFromEvent:e];
    if (self.pointer.on_pointer_up)   self.pointer.on_pointer_up(self.handle, (i32)p.x, (i32)p.y, mel_gui_user(self.handle));
    if (self.pointer.on_click)        self.pointer.on_click(self.handle, mel_gui_user(self.handle));
}

- (void)keyDown:(NSEvent*)e { mel_gui__macos_key(self.handle, self.keyboard, e, true);  [super keyDown:e]; }
- (void)keyUp:(NSEvent*)e   { mel_gui__macos_key(self.handle, self.keyboard, e, false); [super keyUp:e]; }

@end

Mel_Gui_Handle mel_panel_create_opt(Mel_Gui_Handle parent, Mel_Panel_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    @autoreleasepool {
        MelGuiContainerView* view = [[MelGuiContainerView alloc] initWithFrame:NSMakeRect(0, 0, n->width, n->height)];
        view.handle   = h;
        view.pointer  = o.pointer;
        view.focus    = o.focus;
        view.keyboard = o.keyboard;
        mel_gui__macos_install_child(n, view);
    }
    return h;
}
