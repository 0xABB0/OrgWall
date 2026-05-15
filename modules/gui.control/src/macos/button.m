#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

#include <gui.control/button.h>
#include <gui.platform.macos/gui.platform.macos.h>

@interface MelButtonRelay : NSObject
@property (nonatomic) Mel_Gui_Handle handle;
- (void)onClick:(id)sender;
@end

@implementation MelButtonRelay
- (void)onClick:(id)sender { (void)sender; mel_gui_send_message(self.handle, MEL_GUI_MSG_CLICK, 0, 0); }
@end

static const void* kMelButtonRelayKey = &kMelButtonRelayKey;

static NSView* mel__button_construct(Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc)
{
    NSButton* btn = [[NSButton alloc] initWithFrame:NSMakeRect(0, 0, desc->w, desc->h)];
    [btn setBezelStyle:NSBezelStyleRounded];
    [btn setButtonType:NSButtonTypeMomentaryPushIn];

    NSString* title = [[NSString alloc] initWithBytes:desc->text.data
                                               length:(NSUInteger)desc->text.len
                                             encoding:NSUTF8StringEncoding];
    [btn setTitle:(title ?: @"")];

    MelButtonRelay* relay = [[MelButtonRelay alloc] init];
    relay.handle = h;
    [btn setTarget:relay];
    [btn setAction:@selector(onClick:)];
    objc_setAssociatedObject(btn, kMelButtonRelayKey, relay, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    return btn;
}

void mel_gui_button_platform_register(Mel_Atom atom)
{
    mel_gui_macos_register_constructor(atom, mel__button_construct);
}
