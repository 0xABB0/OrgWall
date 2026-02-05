#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "../ui.native.splitview.h"

static const void* kSplitViewDelegateKey = &kSplitViewDelegateKey;

@interface MelSplitViewDelegate : NSObject <NSSplitViewDelegate>
@property (nonatomic, assign) Mel_NSplitView* sv;
@end

@implementation MelSplitViewDelegate
@end

static void nsplitview_create_backing(Mel_NCtrl* ctrl)
{
    Mel_NSplitView* sv = (Mel_NSplitView*)ctrl;

    NSRect frame = NSMakeRect(
        (CGFloat)ctrl->pos.x,
        (CGFloat)ctrl->pos.y,
        (CGFloat)ctrl->size.x,
        (CGFloat)ctrl->size.y
    );

    NSSplitView* split = [[NSSplitView alloc] initWithFrame:frame];
    [split setVertical:(sv->orientation == 0)];
    [split setDividerStyle:NSSplitViewDividerStyleThin];

    MelSplitViewDelegate* delegate = [[MelSplitViewDelegate alloc] init];
    delegate.sv = sv;
    [split setDelegate:delegate];

    objc_setAssociatedObject(split, kSplitViewDelegateKey, delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    ctrl->backing = (__bridge_retained void*)split;
}

static void nsplitview_destroy_backing(Mel_NCtrl* ctrl)
{
    if (!ctrl->backing) return;

    NSSplitView* split = (__bridge NSSplitView*)ctrl->backing;
    [split setDelegate:nil];
    objc_setAssociatedObject(split, kSplitViewDelegateKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    NSSplitView* transfer = (__bridge_transfer NSSplitView*)ctrl->backing;
    [transfer removeFromSuperview];
    (void)transfer;
    ctrl->backing = nullptr;
}

static void nsplitview_set_frame(Mel_NCtrl* ctrl, f32 x, f32 y, f32 w, f32 h)
{
    NSSplitView* split = (__bridge NSSplitView*)ctrl->backing;
    [split setFrame:NSMakeRect(x, y, w, h)];
}

static void nsplitview_set_visible(Mel_NCtrl* ctrl, bool visible)
{
    NSSplitView* split = (__bridge NSSplitView*)ctrl->backing;
    [split setHidden:!visible];
}

static void nsplitview_set_enabled(Mel_NCtrl* ctrl, bool enabled)
{
    (void)ctrl;
    (void)enabled;
}

static Mel_Vec2 nsplitview_preferred_size(Mel_NCtrl* ctrl)
{
    (void)ctrl;
    return mel_vec2(400.0f, 300.0f);
}

static void nsplitview_add_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    if (!parent->backing || !child->backing)
        return;

    NSSplitView* split = (__bridge NSSplitView*)parent->backing;
    NSView* child_view = (__bridge NSView*)child->backing;
    [split addSubview:child_view];

    Mel_NSplitView* sv = (Mel_NSplitView*)parent;
    if ([[split subviews] count] == 2 && sv->divider_position > 0.0f) {
        CGFloat total;
        if ([split isVertical])
            total = [split frame].size.width;
        else
            total = [split frame].size.height;
        [split setPosition:(total * sv->divider_position) ofDividerAtIndex:0];
    }
}

static void nsplitview_remove_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    if (!child->backing)
        return;

    NSView* child_view = (__bridge NSView*)child->backing;
    [child_view removeFromSuperview];
}

static const Mel_NCtrl_VTable s_nsplitview_vtable = {
    .create_backing       = nsplitview_create_backing,
    .destroy_backing      = nsplitview_destroy_backing,
    .set_frame            = nsplitview_set_frame,
    .set_visible          = nsplitview_set_visible,
    .set_enabled          = nsplitview_set_enabled,
    .preferred_size       = nsplitview_preferred_size,
    .add_child_backing    = nsplitview_add_child_backing,
    .remove_child_backing = nsplitview_remove_child_backing,
};

const Mel_NCtrl_VTable* mel__nsplitview_vtable_osx(void)
{
    return &s_nsplitview_vtable;
}

void mel__nsplitview_set_divider_platform(Mel_NSplitView* sv, f32 position)
{
    NSSplitView* split = (__bridge NSSplitView*)sv->base.backing;

    CGFloat total;
    if ([split isVertical])
        total = [split frame].size.width;
    else
        total = [split frame].size.height;

    [split setPosition:(total * position) ofDividerAtIndex:0];
}
