#pragma once

#include "../gui_internal.h"

#ifdef __OBJC__
#import <Cocoa/Cocoa.h>

@interface MelGuiContentView : NSView
@property (assign) Mel_Gui_Handle frame_handle;
@end

@interface MelGuiWindowDelegate : NSObject <NSWindowDelegate>
@property (assign) Mel_Gui_Handle       frame_handle;
@property (assign) Mel_Gui_Lifecycle_Cb lifecycle;
@end

@interface MelGuiButton : NSButton
@property (assign) Mel_Gui_Handle      handle;
@property (assign) Mel_Gui_Pointer_Cb  pointer;
@property (assign) Mel_Gui_Focus_Cb    focus;
@property (assign) Mel_Gui_Keyboard_Cb keyboard;
@end

@interface MelGuiCheckBox : NSButton
@property (assign) Mel_Gui_Handle      handle;
@property (assign) Mel_CheckBox_On     on_;
@property (assign) Mel_Gui_Focus_Cb    focus;
@property (assign) Mel_Gui_Keyboard_Cb keyboard;
@end

@interface MelGuiSlider : NSSlider
@property (assign) Mel_Gui_Handle      handle;
@property (assign) Mel_Slider_On       on_;
@property (assign) Mel_Gui_Focus_Cb    focus;
@property (assign) Mel_Gui_Keyboard_Cb keyboard;
@end

@interface MelGuiTextField : NSTextField
@property (assign) Mel_Gui_Handle      handle;
@property (assign) Mel_TextField_On    on_;
@property (assign) Mel_Gui_Focus_Cb    focus;
@property (assign) Mel_Gui_Keyboard_Cb keyboard;
@end

@interface MelGuiTextFieldDelegate : NSObject <NSTextFieldDelegate>
@property (assign) Mel_Gui_Handle handle;
@end

@interface MelGuiLabel : NSTextField
@property (assign) Mel_Gui_Handle handle;
@end

@interface MelGuiCanvasView : NSView
@property (assign) Mel_Gui_Handle      handle;
@property (assign) Mel_Gui_Pointer_Cb  pointer;
@property (assign) Mel_Gui_Focus_Cb    focus;
@property (assign) Mel_Gui_Keyboard_Cb keyboard;
@property (assign) Mel_Canvas_On       on_;
@end

NSString* mel_gui__macos_nsstring   (str8 s);
NSView*   mel_gui__macos_parent_view (Mel_Gui_Node* n);
void      mel_gui__macos_install_child(Mel_Gui_Node* n, NSView* view);
void      mel_gui__macos_focus_in    (Mel_Gui_Handle h, Mel_Gui_Focus_Cb fc);
void      mel_gui__macos_focus_out   (Mel_Gui_Handle h, Mel_Gui_Focus_Cb fc);
void      mel_gui__macos_key         (Mel_Gui_Handle h, Mel_Gui_Keyboard_Cb kc,
                                      NSEvent* e, bool down);
Mel_Key   mel_gui__macos_key_for_event(NSEvent* e);
NSText*   mel_gui__macos_field_editor (NSWindow* window, id client);
#endif
