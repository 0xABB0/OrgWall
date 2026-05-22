#include "macos.h"

@implementation MelGuiCheckBox

- (BOOL)acceptsFirstResponder { return YES; }

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

- (void)melGuiToggled:(id)sender
{
    (void)sender;
    Mel_Gui_Widget* w = mel_gui__get(self.handle);
    if (!w) return;
    Mel_Gui_CheckBox_Impl* impl = (Mel_Gui_CheckBox_Impl*)w->impl;
    if (!impl || !impl->on_.on_toggled) return;
    bool checked = (self.state == NSControlStateValueOn);
    impl->on_.on_toggled(self.handle, checked, w->user);
}

@end

void mel_gui__backend_checkbox_create(Mel_Gui_Widget* w, str8 text)
{
    @autoreleasepool {
        MelGuiCheckBox* box = [[MelGuiCheckBox alloc] initWithFrame:NSMakeRect(0, 0, w->width, w->height)];
        box.handle     = w->self;
        box.buttonType = NSButtonTypeSwitch;
        box.title      = mel_gui__macos_nsstring(text);
        box.target     = box;
        box.action     = @selector(melGuiToggled:);

        Mel_Gui_CheckBox_Impl* impl = (Mel_Gui_CheckBox_Impl*)w->impl;
        if (impl) {
            box.state = impl->initial_checked ? NSControlStateValueOn : NSControlStateValueOff;
        }

        mel_gui__macos_install_child(w, box);
    }
}

bool mel_gui__backend_checkbox_checked(Mel_Gui_Widget* w)
{
    if (!w || !w->native) return false;
    NSButton* box = (__bridge NSButton*)w->native;
    return box.state == NSControlStateValueOn;
}
