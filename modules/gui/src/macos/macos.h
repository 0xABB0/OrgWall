#pragma once

#include "../gui_internal.h"

#ifdef __OBJC__
#import <Cocoa/Cocoa.h>

@interface MelGuiContentView : NSView
@property (assign) Mel_Gui_Handle frame_handle;
@end

@interface MelGuiWindowDelegate : NSObject <NSWindowDelegate>
@property (assign) Mel_Gui_Handle frame_handle;
@end

@interface MelGuiButton : NSButton
@property (assign) Mel_Gui_Handle handle;
@end

@interface MelGuiCheckBox : NSButton
@property (assign) Mel_Gui_Handle handle;
@end

@interface MelGuiSlider : NSSlider
@property (assign) Mel_Gui_Handle handle;
@end

@interface MelGuiTextField : NSTextField
@property (assign) Mel_Gui_Handle handle;
@end

@interface MelGuiTextFieldDelegate : NSObject <NSTextFieldDelegate>
@property (assign) Mel_Gui_Handle handle;
@end

@interface MelGuiLabel : NSTextField
@property (assign) Mel_Gui_Handle handle;
@end

@interface MelGuiCanvasView : NSView
@property (assign) Mel_Gui_Handle handle;
@end

NSString* mel_gui__macos_nsstring (str8 s);
NSView*   mel_gui__macos_parent_view(Mel_Gui_Widget* w);
void      mel_gui__macos_install_child(Mel_Gui_Widget* w, NSView* view);
void      mel_gui__macos_fire_focus_in (Mel_Gui_Handle h);
void      mel_gui__macos_fire_focus_out(Mel_Gui_Handle h);
Mel_Key   mel_gui__macos_key_for_event(NSEvent* e);
#endif
