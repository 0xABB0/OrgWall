#pragma once

#include <gui/handle.h>

#ifdef __OBJC__
#import <Cocoa/Cocoa.h>

NSWindow* mel_gui_appkit_nswindow(Mel_Gui_Handle h);
NSView*   mel_gui_appkit_nsview  (Mel_Gui_Handle h);
#endif
