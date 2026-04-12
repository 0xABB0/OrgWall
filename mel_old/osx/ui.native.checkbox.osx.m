#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "../ui.native.checkbox.h"
#include "../string.str8.h"

static const void* kCheckboxTargetKey = &kCheckboxTargetKey;

@interface MelCheckboxTarget : NSObject
@property (nonatomic, assign) Mel_NCheckbox* checkbox;
@end

@implementation MelCheckboxTarget

- (void)toggled:(id)sender
{
    NSButton* ns = (NSButton*)sender;
    bool checked = ([ns state] == NSControlStateValueOn);
    _checkbox->checked = checked;
    if (_checkbox->on_change)
        _checkbox->on_change(checked, _checkbox->user_data);
}

@end

static void ncheckbox_create_backing(Mel_NCtrl* ctrl)
{
    Mel_NCheckbox* checkbox = (Mel_NCheckbox*)ctrl;

    MelCheckboxTarget* target = [[MelCheckboxTarget alloc] init];
    target.checkbox = checkbox;

    char text_buf[1024];
    str8_to_buf(checkbox->text, text_buf, sizeof(text_buf));
    NSString* title = [NSString stringWithUTF8String:text_buf];
    NSButton* ns = [NSButton checkboxWithTitle:title target:target action:@selector(toggled:)];
    [ns setTranslatesAutoresizingMaskIntoConstraints:YES];
    [ns setButtonType:NSButtonTypeSwitch];
    [ns setState:checkbox->checked ? NSControlStateValueOn : NSControlStateValueOff];

    objc_setAssociatedObject(ns, kCheckboxTargetKey, target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    ctrl->backing = (__bridge_retained void*)ns;
}

static void ncheckbox_destroy_backing(Mel_NCtrl* ctrl)
{
    if (!ctrl->backing) return;
    NSButton* ns = (__bridge_transfer NSButton*)ctrl->backing;
    objc_setAssociatedObject(ns, kCheckboxTargetKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    (void)ns;
    ctrl->backing = nullptr;
}

static void ncheckbox_set_frame(Mel_NCtrl* ctrl, f32 x, f32 y, f32 w, f32 h)
{
    NSButton* ns = (__bridge NSButton*)ctrl->backing;
    [ns setFrame:NSMakeRect(x, y, w, h)];
}

static void ncheckbox_set_visible(Mel_NCtrl* ctrl, bool visible)
{
    NSButton* ns = (__bridge NSButton*)ctrl->backing;
    [ns setHidden:!visible];
}

static void ncheckbox_set_enabled(Mel_NCtrl* ctrl, bool enabled)
{
    NSButton* ns = (__bridge NSButton*)ctrl->backing;
    [ns setEnabled:enabled];
}

static Mel_Vec2 ncheckbox_preferred_size(Mel_NCtrl* ctrl)
{
    NSButton* ns = (__bridge NSButton*)ctrl->backing;
    NSSize sz = [ns fittingSize];
    return mel_vec2((f32)sz.width, (f32)sz.height);
}

static void ncheckbox_add_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    (void)child;
}

static void ncheckbox_remove_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    (void)child;
}

static const Mel_NCtrl_VTable s_ncheckbox_vtable = {
    .create_backing       = ncheckbox_create_backing,
    .destroy_backing      = ncheckbox_destroy_backing,
    .set_frame            = ncheckbox_set_frame,
    .set_visible          = ncheckbox_set_visible,
    .set_enabled          = ncheckbox_set_enabled,
    .preferred_size       = ncheckbox_preferred_size,
    .add_child_backing    = ncheckbox_add_child_backing,
    .remove_child_backing = ncheckbox_remove_child_backing,
};

const Mel_NCtrl_VTable* mel__ncheckbox_vtable(void)
{
    return &s_ncheckbox_vtable;
}

void mel__ncheckbox_set_text_platform(Mel_NCheckbox* checkbox, const char* text)
{
    NSButton* ns = (__bridge NSButton*)checkbox->base.backing;
    [ns setTitle:[NSString stringWithUTF8String:text]];
}

void mel__ncheckbox_set_checked_platform(Mel_NCheckbox* checkbox, bool checked)
{
    NSButton* ns = (__bridge NSButton*)checkbox->base.backing;
    [ns setState:checked ? NSControlStateValueOn : NSControlStateValueOff];
}
