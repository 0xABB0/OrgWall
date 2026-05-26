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
        mel_gui__macos_key(tf.handle, tf.keyboard, e, true);
        NSString* chars = e.characters;
        if (chars.length > 0 && tf.keyboard.on_char) {
            tf.keyboard.on_char(tf.handle, (u32)[chars characterAtIndex:0], mel_gui_user(tf.handle));
        }
    }
    [super keyDown:e];
}

- (void)keyUp:(NSEvent*)e
{
    MelGuiTextField* tf = [self mel_owning_textfield];
    if (tf) mel_gui__macos_key(tf.handle, tf.keyboard, e, false);
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

Mel_Gui_Handle mel_textfield_create_opt(Mel_Gui_Handle parent, Mel_TextField_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    @autoreleasepool {
        MelGuiTextField* tf = [[MelGuiTextField alloc] initWithFrame:NSMakeRect(0, 0, n->width, n->height)];
        tf.handle          = h;
        tf.on_             = o.on_;
        tf.focus           = o.focus;
        tf.keyboard        = o.keyboard;
        tf.editable        = YES;
        tf.selectable      = YES;
        tf.bezeled         = YES;
        tf.drawsBackground = YES;
        tf.stringValue     = mel_gui__macos_nsstring(o.text);
        if (o.disabled) tf.enabled = NO;

        MelGuiTextFieldDelegate* delegate = [[MelGuiTextFieldDelegate alloc] init];
        delegate.handle = h;
        tf.delegate     = delegate;
        objc_setAssociatedObject(tf, "mel_gui_tf_delegate", delegate,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);

        mel_gui__macos_install_child(n, tf);
    }
    return h;
}
