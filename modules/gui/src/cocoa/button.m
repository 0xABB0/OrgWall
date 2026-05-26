#include "macos.h"

@implementation MelGuiButton

- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)canBecomeKeyView      { return YES; }

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

- (void)melGuiClicked:(id)sender
{
    (void)sender;
    if (self.pointer.on_click) self.pointer.on_click(self.handle, mel_gui_user(self.handle));
}

- (void)keyDown:(NSEvent*)e { mel_gui__macos_key(self.handle, self.keyboard, e, true);  [super keyDown:e]; }
- (void)keyUp:(NSEvent*)e   { mel_gui__macos_key(self.handle, self.keyboard, e, false); [super keyUp:e]; }

@end

Mel_Gui_Handle mel_button_create_opt(Mel_Gui_Handle parent, Mel_Button_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    @autoreleasepool {
        MelGuiButton* button = [[MelGuiButton alloc] initWithFrame:NSMakeRect(0, 0, n->width, n->height)];
        button.handle      = h;
        button.pointer     = o.pointer;
        button.focus       = o.focus;
        button.keyboard    = o.keyboard;
        button.bezelStyle  = NSBezelStyleRegularSquare;
        button.buttonType  = NSButtonTypeMomentaryPushIn;
        button.title       = mel_gui__macos_nsstring(o.text);
        button.target      = button;
        button.action      = @selector(melGuiClicked:);
        if (o.disabled) button.enabled = NO;

        mel_gui__macos_install_child(n, button);
    }
    return h;
}
