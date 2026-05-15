#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

#include <gui.control/checkbox.h>
#include <gui.platform.macos/gui.platform.macos.h>

@interface MelCheckboxRelay : NSObject
@property (nonatomic) Mel_Gui_Handle handle;
- (void)onChange:(id)sender;
@end

@implementation MelCheckboxRelay
- (void)onChange:(id)sender
{
    NSButton* b = sender;
    Mel_Gui_WParam value = ([b state] == NSControlStateValueOn) ? 1 : 0;
    mel_gui_send_message(self.handle, MEL_GUI_MSG_VALUE_CHANGED, value, 0);
}
@end

static const void* kMelCheckboxRelayKey = &kMelCheckboxRelayKey;

static NSView* mel__checkbox_construct(Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc)
{
    NSButton* b = [[NSButton alloc] initWithFrame:NSMakeRect(0, 0, desc->w, desc->h)];
    [b setButtonType:NSButtonTypeSwitch];

    NSString* title = [[NSString alloc] initWithBytes:desc->text.data
                                               length:(NSUInteger)desc->text.len
                                             encoding:NSUTF8StringEncoding];
    [b setTitle:(title ?: @"")];

    MelCheckboxRelay* relay = [[MelCheckboxRelay alloc] init];
    relay.handle = h;
    [b setTarget:relay];
    [b setAction:@selector(onChange:)];
    objc_setAssociatedObject(b, kMelCheckboxRelayKey, relay, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    return b;
}

void mel_gui_checkbox_platform_register(Mel_Atom atom)
{
    mel_gui_macos_register_constructor(atom, mel__checkbox_construct);
}
