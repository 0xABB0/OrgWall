#import <Cocoa/Cocoa.h>

#include <gui.control/panel.h>
#include <gui.platform.macos/gui.platform.macos.h>

static NSView* mel__panel_construct(Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc)
{
    (void)h;
    NSView* v = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, desc->w, desc->h)];
    return v;
}

void mel_gui_panel_platform_register(Mel_Atom atom)
{
    mel_gui_macos_register_constructor(atom, mel__panel_construct);
}
