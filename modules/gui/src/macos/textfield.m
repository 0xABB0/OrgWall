#include "macos.h"

#import <objc/runtime.h>

@implementation MelGuiTextField
- (BOOL)canBecomeKeyView { return YES; }
@end

void mel_gui__backend_textfield_create(Mel_Gui_Widget* w, str8 text)
{
    @autoreleasepool {
        MelGuiTextField* tf = [[MelGuiTextField alloc] initWithFrame:NSMakeRect(0, 0, w->width, w->height)];
        tf.handle          = w->self;
        tf.editable        = YES;
        tf.selectable      = YES;
        tf.bezeled         = YES;
        tf.drawsBackground = YES;
        tf.stringValue     = mel_gui__macos_nsstring(text);

        MelGuiTextFieldDelegate* delegate = [[MelGuiTextFieldDelegate alloc] init];
        delegate.handle = w->self;
        tf.delegate     = delegate;
        objc_setAssociatedObject(tf, "mel_gui_tf_delegate", delegate,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);

        mel_gui__macos_install_child(w, tf);
    }
}
