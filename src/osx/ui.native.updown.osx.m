#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "../ui.native.updown.h"

@interface MelUpDownTarget : NSObject
@property (nonatomic, assign) Mel_NUpDown* mel_updown;
@end

@implementation MelUpDownTarget

- (void)stepperChanged:(id)sender
{
    NSStepper* nsStepper = (NSStepper*)sender;
    if (!self.mel_updown)
        return;

    self.mel_updown->value = [nsStepper doubleValue];

    if (self.mel_updown->on_change)
        self.mel_updown->on_change(self.mel_updown->value, self.mel_updown->user_data);
}

@end

static const void* kUpDownTargetKey = &kUpDownTargetKey;

static void updown_create_backing(Mel_NCtrl* ctrl)
{
    Mel_NUpDown* updown = (Mel_NUpDown*)ctrl;

    NSRect frame = NSMakeRect(
        (CGFloat)ctrl->pos.x,
        (CGFloat)ctrl->pos.y,
        (CGFloat)ctrl->size.x,
        (CGFloat)ctrl->size.y
    );

    NSStepper* nsStepper = [[NSStepper alloc] initWithFrame:frame];
    [nsStepper setMinValue:updown->min];
    [nsStepper setMaxValue:updown->max];
    [nsStepper setIncrement:updown->increment];
    [nsStepper setDoubleValue:updown->value];

    MelUpDownTarget* target = [[MelUpDownTarget alloc] init];
    target.mel_updown = updown;
    [nsStepper setTarget:target];
    [nsStepper setAction:@selector(stepperChanged:)];
    objc_setAssociatedObject(nsStepper, kUpDownTargetKey, target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    ctrl->backing = (__bridge_retained void*)nsStepper;
}

static void updown_destroy_backing(Mel_NCtrl* ctrl)
{
    if (!ctrl->backing)
        return;

    NSStepper* nsStepper = (__bridge_transfer NSStepper*)ctrl->backing;
    [nsStepper removeFromSuperview];
    (void)nsStepper;
    ctrl->backing = nullptr;
}

static void updown_set_frame(Mel_NCtrl* ctrl, f32 x, f32 y, f32 w, f32 h)
{
    if (!ctrl->backing)
        return;

    NSStepper* nsStepper = (__bridge NSStepper*)ctrl->backing;
    [nsStepper setFrame:NSMakeRect((CGFloat)x, (CGFloat)y, (CGFloat)w, (CGFloat)h)];
}

static void updown_set_visible(Mel_NCtrl* ctrl, bool visible)
{
    if (!ctrl->backing)
        return;

    NSStepper* nsStepper = (__bridge NSStepper*)ctrl->backing;
    [nsStepper setHidden:!visible];
}

static void updown_set_enabled(Mel_NCtrl* ctrl, bool enabled)
{
    if (!ctrl->backing)
        return;

    NSStepper* nsStepper = (__bridge NSStepper*)ctrl->backing;
    [nsStepper setEnabled:enabled];
}

static Mel_Vec2 updown_preferred_size(Mel_NCtrl* ctrl)
{
    (void)ctrl;
    return mel_vec2(19, 27);
}

static void updown_add_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    (void)child;
}

static void updown_remove_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    (void)child;
}

static const Mel_NCtrl_VTable s_updown_vtable = {
    .create_backing       = updown_create_backing,
    .destroy_backing      = updown_destroy_backing,
    .set_frame            = updown_set_frame,
    .set_visible          = updown_set_visible,
    .set_enabled          = updown_set_enabled,
    .preferred_size       = updown_preferred_size,
    .add_child_backing    = updown_add_child_backing,
    .remove_child_backing = updown_remove_child_backing,
};

const Mel_NCtrl_VTable* mel__nupdown_vtable_osx(void)
{
    return &s_updown_vtable;
}

void mel__nupdown_set_value_platform(Mel_NUpDown* updown, f64 value)
{
    assert(updown != nullptr);
    if (!updown->base.backing)
        return;

    NSStepper* nsStepper = (__bridge NSStepper*)updown->base.backing;
    [nsStepper setDoubleValue:value];
}

void mel__nupdown_set_range_platform(Mel_NUpDown* updown, f64 min, f64 max)
{
    assert(updown != nullptr);
    if (!updown->base.backing)
        return;

    NSStepper* nsStepper = (__bridge NSStepper*)updown->base.backing;
    [nsStepper setMinValue:min];
    [nsStepper setMaxValue:max];
}
