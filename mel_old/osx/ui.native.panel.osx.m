#import <Cocoa/Cocoa.h>
#include "../ui.native.panel.h"

@interface MelPanelView : NSView
@end

@implementation MelPanelView

- (BOOL)isFlipped
{
    return YES;
}

@end

static void panel_create_backing(Mel_NCtrl* ctrl)
{
    NSRect frame = NSMakeRect(
        (CGFloat)ctrl->pos.x,
        (CGFloat)ctrl->pos.y,
        (CGFloat)ctrl->size.x,
        (CGFloat)ctrl->size.y
    );

    MelPanelView* view = [[MelPanelView alloc] initWithFrame:frame];
    [view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    ctrl->backing = (__bridge_retained void*)view;
}

static void panel_destroy_backing(Mel_NCtrl* ctrl)
{
    if (!ctrl->backing)
        return;

    NSView* view = (__bridge_transfer NSView*)ctrl->backing;
    [view removeFromSuperview];
    ctrl->backing = nullptr;
}

static void panel_set_frame(Mel_NCtrl* ctrl, f32 x, f32 y, f32 w, f32 h)
{
    if (!ctrl->backing)
        return;

    NSView* view = (__bridge NSView*)ctrl->backing;
    [view setFrame:NSMakeRect((CGFloat)x, (CGFloat)y, (CGFloat)w, (CGFloat)h)];
}

static void panel_set_visible(Mel_NCtrl* ctrl, bool visible)
{
    if (!ctrl->backing)
        return;

    NSView* view = (__bridge NSView*)ctrl->backing;
    [view setHidden:!visible];
}

static void panel_set_enabled(Mel_NCtrl* ctrl, bool enabled)
{
    (void)ctrl;
    (void)enabled;
}

static void panel_sync_frame(Mel_NCtrl* ctrl)
{
    if (!ctrl->backing)
        return;

    NSView* view = (__bridge NSView*)ctrl->backing;
    NSRect frame = [view frame];
    ctrl->pos  = mel_vec2((f32)frame.origin.x, (f32)frame.origin.y);
    ctrl->size = mel_vec2((f32)frame.size.width, (f32)frame.size.height);
}

static Mel_Vec2 panel_preferred_size(Mel_NCtrl* ctrl)
{
    return ctrl->size;
}

static void panel_add_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    if (!parent->backing || !child->backing)
        return;

    NSView* parent_view = (__bridge NSView*)parent->backing;
    NSView* child_view  = (__bridge NSView*)child->backing;
    [parent_view addSubview:child_view];
}

static void panel_remove_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    if (!child->backing)
        return;

    NSView* child_view = (__bridge NSView*)child->backing;
    [child_view removeFromSuperview];
}

static const Mel_NCtrl_VTable s_panel_vtable = {
    .create_backing       = panel_create_backing,
    .destroy_backing      = panel_destroy_backing,
    .set_frame            = panel_set_frame,
    .sync_frame           = panel_sync_frame,
    .set_visible          = panel_set_visible,
    .set_enabled          = panel_set_enabled,
    .preferred_size       = panel_preferred_size,
    .add_child_backing    = panel_add_child_backing,
    .remove_child_backing = panel_remove_child_backing,
};

const Mel_NCtrl_VTable* mel__npanel_vtable(void)
{
    return &s_panel_vtable;
}
