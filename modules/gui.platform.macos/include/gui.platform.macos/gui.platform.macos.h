#pragma once

#include <gui/gui.h>

#ifdef __OBJC__
@class NSView;
@class NSWindow;
@class NSApplication;
typedef NSView*        Mel_Macos_View;
typedef NSWindow*      Mel_Macos_Window;
typedef NSApplication* Mel_Macos_App;
#else
typedef void* Mel_Macos_View;
typedef void* Mel_Macos_Window;
typedef void* Mel_Macos_App;
#endif

typedef Mel_Macos_View (*Mel_Gui_Macos_Construct)(Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc);

bool mel_gui_macos_register_constructor(Mel_Atom atom, Mel_Gui_Macos_Construct cb);

Mel_Macos_Window mel_gui_macos_window(void);
Mel_Macos_View   mel_gui_macos_root(void);

void mel_gui_macos_dispatch_main(void (*setup)(void));
