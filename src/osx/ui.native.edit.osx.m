#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "../ui.native.edit.h"

static const void* kEditDelegateKey = &kEditDelegateKey;

@interface MelEditDelegate : NSObject <NSTextFieldDelegate>
@property (nonatomic, assign) Mel_NEdit* edit;
@end

@implementation MelEditDelegate

- (void)controlTextDidChange:(NSNotification*)notification
{
    (void)notification;
    if (_edit && _edit->on_change) {
        NSTextField* tf = (__bridge NSTextField*)_edit->base.backing;
        const char* text = [[tf stringValue] UTF8String];
        _edit->on_change(text, _edit->user_data);
    }
}

- (BOOL)control:(NSControl*)control textView:(NSTextView*)textView doCommandBySelector:(SEL)commandSelector
{
    (void)control;
    (void)textView;
    if (commandSelector == @selector(insertNewline:)) {
        if (_edit && _edit->on_confirm) {
            NSTextField* tf = (__bridge NSTextField*)_edit->base.backing;
            const char* text = [[tf stringValue] UTF8String];
            _edit->on_confirm(text, _edit->user_data);
        }
        return YES;
    }
    return NO;
}

@end

static void nedit_create_backing(Mel_NCtrl* ctrl)
{
    Mel_NEdit* edit = (Mel_NEdit*)ctrl;

    NSTextField* tf = [[NSTextField alloc] initWithFrame:NSMakeRect(0, 0, 200, 22)];
    [tf setStringValue:[NSString stringWithUTF8String:edit->text]];
    [tf setPlaceholderString:[NSString stringWithUTF8String:edit->placeholder]];
    [tf setEditable:YES];
    [tf setSelectable:YES];
    [tf setBezeled:YES];
    [tf setBezelStyle:NSTextFieldSquareBezel];
    [tf setDrawsBackground:YES];

    MelEditDelegate* delegate = [[MelEditDelegate alloc] init];
    delegate.edit = edit;
    [tf setDelegate:delegate];

    objc_setAssociatedObject(tf, kEditDelegateKey, delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    ctrl->backing = (__bridge_retained void*)tf;
}

static void nedit_destroy_backing(Mel_NCtrl* ctrl)
{
    if (!ctrl->backing) return;
    NSTextField* tf = (__bridge_transfer NSTextField*)ctrl->backing;
    [tf setDelegate:nil];
    objc_setAssociatedObject(tf, kEditDelegateKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    (void)tf;
    ctrl->backing = nullptr;
}

static void nedit_set_frame(Mel_NCtrl* ctrl, f32 x, f32 y, f32 w, f32 h)
{
    NSTextField* tf = (__bridge NSTextField*)ctrl->backing;
    [tf setFrame:NSMakeRect(x, y, w, h)];
}

static void nedit_set_visible(Mel_NCtrl* ctrl, bool visible)
{
    NSTextField* tf = (__bridge NSTextField*)ctrl->backing;
    [tf setHidden:!visible];
}

static void nedit_set_enabled(Mel_NCtrl* ctrl, bool enabled)
{
    NSTextField* tf = (__bridge NSTextField*)ctrl->backing;
    [tf setEnabled:enabled];
}

static Mel_Vec2 nedit_preferred_size(Mel_NCtrl* ctrl)
{
    (void)ctrl;
    return mel_vec2(200.0f, 22.0f);
}

static void nedit_add_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    (void)child;
}

static void nedit_remove_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    (void)child;
}

static const Mel_NCtrl_VTable s_nedit_vtable = {
    .create_backing       = nedit_create_backing,
    .destroy_backing      = nedit_destroy_backing,
    .set_frame            = nedit_set_frame,
    .set_visible          = nedit_set_visible,
    .set_enabled          = nedit_set_enabled,
    .preferred_size       = nedit_preferred_size,
    .add_child_backing    = nedit_add_child_backing,
    .remove_child_backing = nedit_remove_child_backing,
};

const Mel_NCtrl_VTable* mel__nedit_vtable_osx(void)
{
    return &s_nedit_vtable;
}

void mel__nedit_set_text_platform(Mel_NEdit* edit, const char* text)
{
    NSTextField* tf = (__bridge NSTextField*)edit->base.backing;
    [tf setStringValue:[NSString stringWithUTF8String:text]];
}

void mel__nedit_set_placeholder_platform(Mel_NEdit* edit, const char* placeholder)
{
    NSTextField* tf = (__bridge NSTextField*)edit->base.backing;
    [tf setPlaceholderString:[NSString stringWithUTF8String:placeholder]];
}

const char* mel__nedit_get_text_platform(Mel_NEdit* edit)
{
    NSTextField* tf = (__bridge NSTextField*)edit->base.backing;
    return [[tf stringValue] UTF8String];
}
