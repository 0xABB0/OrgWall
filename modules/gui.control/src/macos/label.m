#import <Cocoa/Cocoa.h>

#include <gui.control/label.h>
#include <gui.platform.macos/gui.platform.macos.h>

static NSView* mel__label_construct(Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc)
{
    (void)h;
    NSTextField* tf = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, desc->w, desc->h)];
    [tf setEditable:NO];
    [tf setBordered:NO];
    [tf setBezeled:NO];
    [tf setDrawsBackground:NO];
    [tf setSelectable:NO];

    if (desc->id == 1) {
        [tf setFont:[NSFont systemFontOfSize:22 weight:NSFontWeightSemibold]];
    }

    NSString* s = [[NSString alloc] initWithBytes:desc->text.data
                                           length:(NSUInteger)desc->text.len
                                         encoding:NSUTF8StringEncoding];
    [tf setStringValue:(s ?: @"")];
    return tf;
}

void mel_gui_label_platform_register(Mel_Atom atom)
{
    mel_gui_macos_register_constructor(atom, mel__label_construct);
}
