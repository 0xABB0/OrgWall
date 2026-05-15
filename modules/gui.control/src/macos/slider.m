#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

#include <gui.control/slider.h>
#include <gui.platform.macos/gui.platform.macos.h>

@interface MelSliderRelay : NSObject
@property (nonatomic) Mel_Gui_Handle handle;
- (void)onChange:(id)sender;
@end

@implementation MelSliderRelay
- (void)onChange:(id)sender
{
    NSSlider* s = sender;
    Mel_Gui_WParam value = (Mel_Gui_WParam)(i64)[s intValue];
    mel_gui_send_message(self.handle, MEL_GUI_MSG_VALUE_CHANGED, value, 0);
}
@end

static const void* kMelSliderRelayKey = &kMelSliderRelayKey;

static NSView* mel__slider_construct(Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc)
{
    NSSlider* s = [[NSSlider alloc] initWithFrame:NSMakeRect(0, 0, desc->w, desc->h)];
    [s setMinValue:0];
    [s setMaxValue:100];
    [s setIntValue:65];
    [s setContinuous:YES];

    MelSliderRelay* relay = [[MelSliderRelay alloc] init];
    relay.handle = h;
    [s setTarget:relay];
    [s setAction:@selector(onChange:)];
    objc_setAssociatedObject(s, kMelSliderRelayKey, relay, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    return s;
}

void mel_gui_slider_platform_register(Mel_Atom atom)
{
    mel_gui_macos_register_constructor(atom, mel__slider_construct);
}
