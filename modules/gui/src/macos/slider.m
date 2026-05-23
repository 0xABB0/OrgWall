#include "macos.h"

@implementation MelGuiSlider

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

- (void)melGuiValueChanged:(id)sender
{
    (void)sender;
    if (self.on_.on_value_changed) {
        self.on_.on_value_changed(self.handle, (i32)self.intValue, mel_gui_user(self.handle));
    }
}

- (void)keyDown:(NSEvent*)e { mel_gui__macos_key(self.handle, self.keyboard, e, true);  [super keyDown:e]; }
- (void)keyUp:(NSEvent*)e   { mel_gui__macos_key(self.handle, self.keyboard, e, false); [super keyUp:e]; }

@end

Mel_Gui_Handle mel_slider_create_opt(Mel_Gui_Handle parent, Mel_Slider_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    i32 max_value = (o.max_value > o.min_value) ? o.max_value : o.min_value + 100;

    @autoreleasepool {
        MelGuiSlider* slider = [[MelGuiSlider alloc] initWithFrame:NSMakeRect(0, 0, n->width, n->height)];
        slider.handle     = h;
        slider.on_        = o.on_;
        slider.focus      = o.focus;
        slider.keyboard   = o.keyboard;
        slider.continuous = YES;
        slider.target     = slider;
        slider.action     = @selector(melGuiValueChanged:);
        slider.minValue   = o.min_value;
        slider.maxValue   = max_value;
        slider.intValue   = o.value;
        if (o.disabled) slider.enabled = NO;

        mel_gui__macos_install_child(n, slider);
    }
    return h;
}

i32 mel_slider_value(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native) return 0;
    NSSlider* s = (__bridge NSSlider*)n->native;
    return (i32)s.intValue;
}

void mel_slider_set_value(Mel_Gui_Handle h, i32 value)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native) return;
    NSSlider* s = (__bridge NSSlider*)n->native;
    s.intValue = value;
}
