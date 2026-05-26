#pragma once

#include "../gui_internal.h"

#ifdef __OBJC__
#import <UIKit/UIKit.h>

struct Mel_Painter { CGContextRef cg; f32 w, h; };

@interface MelView : UIView
@property (assign) Mel_Gui_Handle      handle;
@property (assign) Mel_Gui_Pointer_Cb  pointer;
@property (assign) Mel_Gui_Focus_Cb    focus;
@end

@interface MelButton : UIButton
@property (assign) Mel_Gui_Handle     handle;
@property (assign) Mel_Gui_Pointer_Cb pointer;
@end

@interface MelLabel : UILabel
@property (assign) Mel_Gui_Handle handle;
@end

@interface MelSwitch : UISwitch
@property (assign) Mel_Gui_Handle  handle;
@property (assign) Mel_CheckBox_On on_;
@end

@interface MelSlider : UISlider
@property (assign) Mel_Gui_Handle handle;
@property (assign) Mel_Slider_On  on_;
@end

@interface MelField : UITextField <UITextFieldDelegate>
@property (assign) Mel_Gui_Handle    handle;
@property (assign) Mel_TextField_On  on_;
@property (assign) Mel_Gui_Focus_Cb  focus;
@end

@interface MelDialogWindow : UIWindow
@property (assign) Mel_Gui_Handle handle;
@property (assign) Mel_Dialog_On  on_;
@end

@interface MelCanvas : UIView
@property (assign) Mel_Gui_Handle     handle;
@property (assign) Mel_Gui_Pointer_Cb pointer;
@property (assign) Mel_Gui_Focus_Cb   focus;
@property (assign) Mel_Canvas_On      on_;
@end

NSString* mel_gui__ios_nsstring     (str8 s);
UIView*   mel_gui__ios_parent_view  (Mel_Gui_Node* n);
void      mel_gui__ios_install_child(Mel_Gui_Node* n, UIView* view);
void      mel_gui__ios_sync         (dispatch_block_t block);
#endif
