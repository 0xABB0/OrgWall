#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>

#include <gui.control/edit.h>
#include <gui.platform.macos/gui.platform.macos.h>

@interface MelEditRelay : NSObject <NSTextFieldDelegate>
@property (nonatomic) Mel_Gui_Handle handle;
@end

@implementation MelEditRelay
- (void)controlTextDidChange:(NSNotification*)notification
{
    NSTextField* tf = notification.object;
    NSString* s = [tf stringValue];
    NSData* data = [s dataUsingEncoding:NSUTF8StringEncoding];
    mel_gui_send_message(self.handle,
                         MEL_GUI_MSG_TEXT_CHANGED,
                         (Mel_Gui_WParam)(usize)data.length,
                         (Mel_Gui_LParam)(intptr_t)data.bytes);
}
@end

static const void* kMelEditRelayKey = &kMelEditRelayKey;

static NSView* mel__edit_construct(Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc)
{
    NSTextField* tf = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, desc->w, desc->h)];
    [tf setEditable:YES];
    [tf setBordered:YES];
    [tf setBezeled:YES];
    [tf setDrawsBackground:YES];

    NSString* s = [[NSString alloc] initWithBytes:desc->text.data
                                           length:(NSUInteger)desc->text.len
                                         encoding:NSUTF8StringEncoding];
    [tf setStringValue:(s ?: @"")];

    MelEditRelay* relay = [[MelEditRelay alloc] init];
    relay.handle = h;
    [tf setDelegate:relay];
    objc_setAssociatedObject(tf, kMelEditRelayKey, relay, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    return tf;
}

void mel_gui_edit_platform_register(Mel_Atom atom)
{
    mel_gui_macos_register_constructor(atom, mel__edit_construct);
}
