#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "../ui.native.slider.h"

@interface MelSliderTarget : NSObject
@property (nonatomic, assign) Mel_NSlider* mel_slider;
@end

@implementation MelSliderTarget

- (void)sliderChanged:(id)sender
{
    NSSlider* nsSlider = (NSSlider*)sender;
    if (!self.mel_slider)
        return;

    self.mel_slider->value = [nsSlider doubleValue];

    if (self.mel_slider->on_change)
        self.mel_slider->on_change(self.mel_slider->value, self.mel_slider->user_data);
}

@end

static const void* kSliderTargetKey = &kSliderTargetKey;

static void slider_create_backing(Mel_NCtrl* ctrl)
{
    Mel_NSlider* slider = (Mel_NSlider*)ctrl;

    NSRect frame = NSMakeRect(
        (CGFloat)ctrl->pos.x,
        (CGFloat)ctrl->pos.y,
        (CGFloat)ctrl->size.x,
        (CGFloat)ctrl->size.y
    );

    NSSlider* nsSlider = [[NSSlider alloc] initWithFrame:frame];
    [nsSlider setMinValue:slider->min];
    [nsSlider setMaxValue:slider->max];
    [nsSlider setDoubleValue:slider->value];
    [nsSlider setContinuous:YES];

    MelSliderTarget* target = [[MelSliderTarget alloc] init];
    target.mel_slider = slider;
    [nsSlider setTarget:target];
    [nsSlider setAction:@selector(sliderChanged:)];
    objc_setAssociatedObject(nsSlider, kSliderTargetKey, target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    ctrl->backing = (__bridge_retained void*)nsSlider;
}

static void slider_destroy_backing(Mel_NCtrl* ctrl)
{
    if (!ctrl->backing)
        return;

    NSSlider* nsSlider = (__bridge_transfer NSSlider*)ctrl->backing;
    [nsSlider removeFromSuperview];
    (void)nsSlider;
    ctrl->backing = nullptr;
}

static void slider_set_frame(Mel_NCtrl* ctrl, f32 x, f32 y, f32 w, f32 h)
{
    if (!ctrl->backing)
        return;

    NSSlider* nsSlider = (__bridge NSSlider*)ctrl->backing;
    [nsSlider setFrame:NSMakeRect((CGFloat)x, (CGFloat)y, (CGFloat)w, (CGFloat)h)];
}

static void slider_set_visible(Mel_NCtrl* ctrl, bool visible)
{
    if (!ctrl->backing)
        return;

    NSSlider* nsSlider = (__bridge NSSlider*)ctrl->backing;
    [nsSlider setHidden:!visible];
}

static void slider_set_enabled(Mel_NCtrl* ctrl, bool enabled)
{
    if (!ctrl->backing)
        return;

    NSSlider* nsSlider = (__bridge NSSlider*)ctrl->backing;
    [nsSlider setEnabled:enabled];
}

static Mel_Vec2 slider_preferred_size(Mel_NCtrl* ctrl)
{
    (void)ctrl;
    return mel_vec2(200, 22);
}

static void slider_add_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    (void)child;
}

static void slider_remove_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    (void)child;
}

static const Mel_NCtrl_VTable s_slider_vtable = {
    .create_backing       = slider_create_backing,
    .destroy_backing      = slider_destroy_backing,
    .set_frame            = slider_set_frame,
    .set_visible          = slider_set_visible,
    .set_enabled          = slider_set_enabled,
    .preferred_size       = slider_preferred_size,
    .add_child_backing    = slider_add_child_backing,
    .remove_child_backing = slider_remove_child_backing,
};

const Mel_NCtrl_VTable* mel__nslider_vtable(void)
{
    return &s_slider_vtable;
}

void mel__nslider_set_value_platform(Mel_NSlider* slider, f64 value)
{
    assert(slider != nullptr);
    if (!slider->base.backing)
        return;

    NSSlider* nsSlider = (__bridge NSSlider*)slider->base.backing;
    [nsSlider setDoubleValue:value];
}

void mel__nslider_set_range_platform(Mel_NSlider* slider, f64 min, f64 max)
{
    assert(slider != nullptr);
    if (!slider->base.backing)
        return;

    NSSlider* nsSlider = (__bridge NSSlider*)slider->base.backing;
    [nsSlider setMinValue:min];
    [nsSlider setMaxValue:max];
}
