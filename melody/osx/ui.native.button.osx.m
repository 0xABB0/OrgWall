#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "../ui.native.button.h"

static const void* kButtonTargetKey = &kButtonTargetKey;

@interface MelButtonTarget : NSObject
@property (nonatomic, assign) Mel_NButton* button;
@end

@implementation MelButtonTarget

- (void)clicked:(id)sender
{
    (void)sender;
    if (_button && _button->on_click)
        _button->on_click(_button->user_data);
}

@end

static void nbutton_create_backing(Mel_NCtrl* ctrl)
{
    Mel_NButton* button = (Mel_NButton*)ctrl;

    MelButtonTarget* target = [[MelButtonTarget alloc] init];
    target.button = button;

    NSString* title = [NSString stringWithUTF8String:button->text];
    NSButton* ns = [NSButton buttonWithTitle:title target:target action:@selector(clicked:)];
    [ns setTranslatesAutoresizingMaskIntoConstraints:YES];
    [ns setBezelStyle:NSBezelStyleRounded];

    objc_setAssociatedObject(ns, kButtonTargetKey, target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    ctrl->backing = (__bridge_retained void*)ns;
}

static void nbutton_destroy_backing(Mel_NCtrl* ctrl)
{
    if (!ctrl->backing) return;
    NSButton* ns = (__bridge_transfer NSButton*)ctrl->backing;
    objc_setAssociatedObject(ns, kButtonTargetKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    (void)ns;
    ctrl->backing = nullptr;
}

static void nbutton_set_frame(Mel_NCtrl* ctrl, f32 x, f32 y, f32 w, f32 h)
{
    NSButton* ns = (__bridge NSButton*)ctrl->backing;
    [ns setFrame:NSMakeRect(x, y, w, h)];
}

static void nbutton_set_visible(Mel_NCtrl* ctrl, bool visible)
{
    NSButton* ns = (__bridge NSButton*)ctrl->backing;
    [ns setHidden:!visible];
}

static void nbutton_set_enabled(Mel_NCtrl* ctrl, bool enabled)
{
    NSButton* ns = (__bridge NSButton*)ctrl->backing;
    [ns setEnabled:enabled];
}

static Mel_Vec2 nbutton_preferred_size(Mel_NCtrl* ctrl)
{
    NSButton* ns = (__bridge NSButton*)ctrl->backing;
    NSSize sz = [ns fittingSize];
    return mel_vec2((f32)sz.width, (f32)sz.height);
}

static void nbutton_add_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    (void)child;
}

static void nbutton_remove_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    (void)child;
}

static const Mel_NCtrl_VTable s_nbutton_vtable = {
    .create_backing       = nbutton_create_backing,
    .destroy_backing      = nbutton_destroy_backing,
    .set_frame            = nbutton_set_frame,
    .set_visible          = nbutton_set_visible,
    .set_enabled          = nbutton_set_enabled,
    .preferred_size       = nbutton_preferred_size,
    .add_child_backing    = nbutton_add_child_backing,
    .remove_child_backing = nbutton_remove_child_backing,
};

const Mel_NCtrl_VTable* mel__nbutton_vtable(void)
{
    return &s_nbutton_vtable;
}

void mel__nbutton_set_text_platform(Mel_NButton* button, const char* text)
{
    NSButton* ns = (__bridge NSButton*)button->base.backing;
    [ns setTitle:[NSString stringWithUTF8String:text]];
}
