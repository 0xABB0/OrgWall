#include "macos.h"

@implementation MelGuiCheckBox

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

- (void)melGuiToggled:(id)sender
{
    (void)sender;
    if (!self.on_.on_toggled) return;
    bool checked = (self.state == NSControlStateValueOn);
    self.on_.on_toggled(self.handle, checked, mel_gui_user(self.handle));
}

- (void)keyDown:(NSEvent*)e { mel_gui__macos_key(self.handle, self.keyboard, e, true);  [super keyDown:e]; }
- (void)keyUp:(NSEvent*)e   { mel_gui__macos_key(self.handle, self.keyboard, e, false); [super keyUp:e]; }

@end

Mel_Gui_Handle mel_checkbox_create_opt(Mel_Gui_Handle parent, Mel_CheckBox_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    @autoreleasepool {
        MelGuiCheckBox* box = [[MelGuiCheckBox alloc] initWithFrame:NSMakeRect(0, 0, n->width, n->height)];
        box.handle     = h;
        box.on_        = o.on_;
        box.focus      = o.focus;
        box.keyboard   = o.keyboard;
        box.buttonType = NSButtonTypeSwitch;
        box.title      = mel_gui__macos_nsstring(o.text);
        box.target     = box;
        box.action     = @selector(melGuiToggled:);
        box.state      = o.checked ? NSControlStateValueOn : NSControlStateValueOff;
        if (o.disabled) box.enabled = NO;

        mel_gui__macos_install_child(n, box);
    }
    return h;
}

bool mel_checkbox_checked(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native) return false;
    NSButton* box = (__bridge NSButton*)n->native;
    return box.state == NSControlStateValueOn;
}
