#import <Cocoa/Cocoa.h>
#include "../ui.native.progress.h"

static void progress_create_backing(Mel_NCtrl* ctrl)
{
    Mel_NProgress* progress = (Mel_NProgress*)ctrl;

    NSRect frame = NSMakeRect(
        (CGFloat)ctrl->pos.x,
        (CGFloat)ctrl->pos.y,
        (CGFloat)ctrl->size.x,
        (CGFloat)ctrl->size.y
    );

    NSProgressIndicator* nsProgress = [[NSProgressIndicator alloc] initWithFrame:frame];
    [nsProgress setStyle:NSProgressIndicatorStyleBar];
    [nsProgress setMinValue:progress->min];
    [nsProgress setMaxValue:progress->max];
    [nsProgress setDoubleValue:progress->value];
    [nsProgress setIndeterminate:progress->indeterminate];

    if (progress->indeterminate)
        [nsProgress startAnimation:nil];

    ctrl->backing = (__bridge_retained void*)nsProgress;
}

static void progress_destroy_backing(Mel_NCtrl* ctrl)
{
    if (!ctrl->backing)
        return;

    NSProgressIndicator* nsProgress = (__bridge_transfer NSProgressIndicator*)ctrl->backing;
    [nsProgress removeFromSuperview];
    (void)nsProgress;
    ctrl->backing = nullptr;
}

static void progress_set_frame(Mel_NCtrl* ctrl, f32 x, f32 y, f32 w, f32 h)
{
    if (!ctrl->backing)
        return;

    NSProgressIndicator* nsProgress = (__bridge NSProgressIndicator*)ctrl->backing;
    [nsProgress setFrame:NSMakeRect((CGFloat)x, (CGFloat)y, (CGFloat)w, (CGFloat)h)];
}

static void progress_set_visible(Mel_NCtrl* ctrl, bool visible)
{
    if (!ctrl->backing)
        return;

    NSProgressIndicator* nsProgress = (__bridge NSProgressIndicator*)ctrl->backing;
    [nsProgress setHidden:!visible];
}

static void progress_set_enabled(Mel_NCtrl* ctrl, bool enabled)
{
    (void)ctrl;
    (void)enabled;
}

static Mel_Vec2 progress_preferred_size(Mel_NCtrl* ctrl)
{
    (void)ctrl;
    return mel_vec2(200, 20);
}

static void progress_add_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    (void)child;
}

static void progress_remove_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    (void)child;
}

static const Mel_NCtrl_VTable s_progress_vtable = {
    .create_backing       = progress_create_backing,
    .destroy_backing      = progress_destroy_backing,
    .set_frame            = progress_set_frame,
    .set_visible          = progress_set_visible,
    .set_enabled          = progress_set_enabled,
    .preferred_size       = progress_preferred_size,
    .add_child_backing    = progress_add_child_backing,
    .remove_child_backing = progress_remove_child_backing,
};

const Mel_NCtrl_VTable* mel__nprogress_vtable(void)
{
    return &s_progress_vtable;
}

void mel__nprogress_set_value_platform(Mel_NProgress* progress, f64 value)
{
    assert(progress != nullptr);
    if (!progress->base.backing)
        return;

    NSProgressIndicator* nsProgress = (__bridge NSProgressIndicator*)progress->base.backing;
    [nsProgress setDoubleValue:value];
}

void mel__nprogress_set_indeterminate_platform(Mel_NProgress* progress, bool indeterminate)
{
    assert(progress != nullptr);
    if (!progress->base.backing)
        return;

    NSProgressIndicator* nsProgress = (__bridge NSProgressIndicator*)progress->base.backing;
    [nsProgress setIndeterminate:indeterminate];

    if (indeterminate)
        [nsProgress startAnimation:nil];
    else
        [nsProgress stopAnimation:nil];
}
