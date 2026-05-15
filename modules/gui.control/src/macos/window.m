#import <Cocoa/Cocoa.h>

#include <gui.control/window.h>
#include <gui.platform.macos/gui.platform.macos.h>

@interface MelWindowView : NSView
@end
@implementation MelWindowView
- (BOOL)isFlipped { return YES; }
@end

static NSView* mel__window_construct(Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc)
{
    (void)h;

    if (desc->text.len > 0) {
        NSString* title = [[NSString alloc] initWithBytes:desc->text.data
                                                   length:(NSUInteger)desc->text.len
                                                 encoding:NSUTF8StringEncoding];
        if (title) [mel_gui_macos_window() setTitle:title];
    }

    NSView* root = mel_gui_macos_root();
    MelWindowView* v = [[MelWindowView alloc] initWithFrame:root.bounds];
    v.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    return v;
}

void mel_gui_window_platform_register(Mel_Atom atom)
{
    mel_gui_macos_register_constructor(atom, mel__window_construct);
}
