#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "../ui.native.radio.h"

@interface MelRadioTarget : NSObject
@property (nonatomic, assign) Mel_NRadio* mel_radio;
@end

@implementation MelRadioTarget

- (void)radioClicked:(id)sender
{
    (void)sender;
    if (!self.mel_radio)
        return;

    Mel_NRadio* radio = self.mel_radio;
    Mel_NCtrl* parent = radio->base.parent;

    if (parent) {
        Mel_NCtrl* sib = parent->first_child;
        while (sib) {
            if (sib->vtable == radio->base.vtable && sib != &radio->base) {
                Mel_NRadio* sibling_radio = (Mel_NRadio*)sib;
                if (sibling_radio->group_id == radio->group_id && sibling_radio->selected) {
                    sibling_radio->selected = false;
                    if (sib->backing) {
                        NSButton* nsBtn = (__bridge NSButton*)sib->backing;
                        [nsBtn setState:NSControlStateValueOff];
                    }
                    if (sibling_radio->on_change)
                        sibling_radio->on_change(false, sibling_radio->user_data);
                }
            }
            sib = sib->next_sibling;
        }
    }

    radio->selected = true;
    NSButton* nsButton = (__bridge NSButton*)radio->base.backing;
    [nsButton setState:NSControlStateValueOn];

    if (radio->on_change)
        radio->on_change(true, radio->user_data);
}

@end

static const void* kRadioTargetKey = &kRadioTargetKey;

static void radio_create_backing(Mel_NCtrl* ctrl)
{
    Mel_NRadio* radio = (Mel_NRadio*)ctrl;

    NSRect frame = NSMakeRect(
        (CGFloat)ctrl->pos.x,
        (CGFloat)ctrl->pos.y,
        (CGFloat)ctrl->size.x,
        (CGFloat)ctrl->size.y
    );

    NSButton* nsButton = [[NSButton alloc] initWithFrame:frame];
    [nsButton setButtonType:NSButtonTypeRadio];
    [nsButton setTitle:[NSString stringWithUTF8String:radio->text]];
    [nsButton setState:radio->selected ? NSControlStateValueOn : NSControlStateValueOff];

    MelRadioTarget* target = [[MelRadioTarget alloc] init];
    target.mel_radio = radio;
    [nsButton setTarget:target];
    [nsButton setAction:@selector(radioClicked:)];
    objc_setAssociatedObject(nsButton, kRadioTargetKey, target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    ctrl->backing = (__bridge_retained void*)nsButton;
}

static void radio_destroy_backing(Mel_NCtrl* ctrl)
{
    if (!ctrl->backing)
        return;

    NSButton* nsButton = (__bridge_transfer NSButton*)ctrl->backing;
    [nsButton removeFromSuperview];
    (void)nsButton;
    ctrl->backing = nullptr;
}

static void radio_set_frame(Mel_NCtrl* ctrl, f32 x, f32 y, f32 w, f32 h)
{
    if (!ctrl->backing)
        return;

    NSButton* nsButton = (__bridge NSButton*)ctrl->backing;
    [nsButton setFrame:NSMakeRect((CGFloat)x, (CGFloat)y, (CGFloat)w, (CGFloat)h)];
}

static void radio_set_visible(Mel_NCtrl* ctrl, bool visible)
{
    if (!ctrl->backing)
        return;

    NSButton* nsButton = (__bridge NSButton*)ctrl->backing;
    [nsButton setHidden:!visible];
}

static void radio_set_enabled(Mel_NCtrl* ctrl, bool enabled)
{
    if (!ctrl->backing)
        return;

    NSButton* nsButton = (__bridge NSButton*)ctrl->backing;
    [nsButton setEnabled:enabled];
}

static Mel_Vec2 radio_preferred_size(Mel_NCtrl* ctrl)
{
    if (!ctrl->backing)
        return mel_vec2(100, 18);

    NSButton* nsButton = (__bridge NSButton*)ctrl->backing;
    NSSize size = [nsButton fittingSize];
    return mel_vec2((f32)size.width, (f32)size.height);
}

static void radio_add_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    (void)child;
}

static void radio_remove_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    (void)child;
}

static const Mel_NCtrl_VTable s_radio_vtable = {
    .create_backing       = radio_create_backing,
    .destroy_backing      = radio_destroy_backing,
    .set_frame            = radio_set_frame,
    .set_visible          = radio_set_visible,
    .set_enabled          = radio_set_enabled,
    .preferred_size       = radio_preferred_size,
    .add_child_backing    = radio_add_child_backing,
    .remove_child_backing = radio_remove_child_backing,
};

const Mel_NCtrl_VTable* mel__nradio_vtable(void)
{
    return &s_radio_vtable;
}

void mel__nradio_set_text_platform(Mel_NRadio* radio, const char* text)
{
    assert(radio != nullptr);
    if (!radio->base.backing)
        return;

    NSButton* nsButton = (__bridge NSButton*)radio->base.backing;
    [nsButton setTitle:[NSString stringWithUTF8String:text]];
}

void mel__nradio_set_selected_platform(Mel_NRadio* radio, bool selected)
{
    assert(radio != nullptr);
    if (!radio->base.backing)
        return;

    NSButton* nsButton = (__bridge NSButton*)radio->base.backing;
    [nsButton setState:selected ? NSControlStateValueOn : NSControlStateValueOff];
}
