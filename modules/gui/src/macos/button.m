#include "macos.h"

@implementation MelGuiButton

- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)canBecomeKeyView      { return YES; }

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

- (void)melGuiClicked:(id)sender
{
    (void)sender;
    mel_gui__fire_click(self.handle);
}

- (void)keyDown:(NSEvent*)e
{
    mel_gui__fire_key_down(self.handle, mel_gui__macos_key_for_event(e));
    [super keyDown:e];
}

- (void)keyUp:(NSEvent*)e
{
    mel_gui__fire_key_up(self.handle, mel_gui__macos_key_for_event(e));
    [super keyUp:e];
}

@end

void mel_gui__backend_button_create(Mel_Gui_Widget* w, str8 text)
{
    @autoreleasepool {
        MelGuiButton* button = [[MelGuiButton alloc] initWithFrame:NSMakeRect(0, 0, w->width, w->height)];
        button.handle      = w->self;
        button.bezelStyle  = NSBezelStyleRegularSquare;
        button.buttonType  = NSButtonTypeMomentaryPushIn;
        button.title       = mel_gui__macos_nsstring(text);
        button.target      = button;
        button.action      = @selector(melGuiClicked:);

        mel_gui__macos_install_child(w, button);
    }
}
