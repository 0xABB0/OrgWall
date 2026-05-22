#include "macos.h"

@implementation MelGuiSlider

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

- (void)melGuiValueChanged:(id)sender
{
    (void)sender;
    Mel_Gui_Widget* w = mel_gui__get(self.handle);
    if (!w) return;
    Mel_Gui_Slider_Impl* impl = (Mel_Gui_Slider_Impl*)w->impl;
    if (!impl) return;
    i32 v = (i32)self.intValue;
    impl->value = v;
    if (impl->on_.on_value_changed) {
        impl->on_.on_value_changed(self.handle, v, w->user);
    }
}

@end

void mel_gui__backend_slider_create(Mel_Gui_Widget* w, str8 text)
{
    (void)text;
    @autoreleasepool {
        MelGuiSlider* slider = [[MelGuiSlider alloc] initWithFrame:NSMakeRect(0, 0, w->width, w->height)];
        slider.handle      = w->self;
        slider.continuous  = YES;
        slider.target      = slider;
        slider.action      = @selector(melGuiValueChanged:);

        Mel_Gui_Slider_Impl* impl = (Mel_Gui_Slider_Impl*)w->impl;
        if (impl) {
            slider.minValue = impl->min_value;
            slider.maxValue = impl->max_value;
            slider.intValue = impl->value;
        }

        mel_gui__macos_install_child(w, slider);
    }
}

i32 mel_gui__backend_slider_value(Mel_Gui_Widget* w)
{
    if (!w || !w->native) return 0;
    NSSlider* s = (__bridge NSSlider*)w->native;
    return (i32)s.intValue;
}

void mel_gui__backend_slider_set_value(Mel_Gui_Widget* w, i32 value)
{
    if (!w || !w->native) return;
    NSSlider* s = (__bridge NSSlider*)w->native;
    s.intValue = value;
}
