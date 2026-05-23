#include "macos.h"

#import <objc/runtime.h>

@implementation MelGuiTextField
- (BOOL)canBecomeKeyView { return YES; }
@end

@interface MelGuiFieldEditor : NSTextView
@end

@implementation MelGuiFieldEditor

- (MelGuiTextField*)mel_owning_textfield
{
    id d = self.delegate;
    return [d isKindOfClass:[MelGuiTextField class]] ? (MelGuiTextField*)d : nil;
}

- (void)keyDown:(NSEvent*)e
{
    MelGuiTextField* tf = [self mel_owning_textfield];
    if (tf) {
        mel_gui__fire_key_down(tf.handle, mel_gui__macos_key_for_event(e));
        NSString* chars = e.characters;
        if (chars.length > 0) {
            mel_gui__fire_char(tf.handle, (u32)[chars characterAtIndex:0]);
        }
    }
    [super keyDown:e];
}

- (void)keyUp:(NSEvent*)e
{
    MelGuiTextField* tf = [self mel_owning_textfield];
    if (tf) {
        mel_gui__fire_key_up(tf.handle, mel_gui__macos_key_for_event(e));
    }
    [super keyUp:e];
}

@end

NSText* mel_gui__macos_field_editor(NSWindow* window, id client)
{
    if (![client isKindOfClass:[MelGuiTextField class]]) return nil;
    MelGuiFieldEditor* fe = objc_getAssociatedObject(window, "mel_gui_field_editor");
    if (!fe) {
        fe = [[MelGuiFieldEditor alloc] init];
        [fe setFieldEditor:YES];
        objc_setAssociatedObject(window, "mel_gui_field_editor", fe,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    }
    return fe;
}

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
