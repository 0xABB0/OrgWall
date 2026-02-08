#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "../ui.native.popup.h"

static const void* kPopupContentViewKey = &kPopupContentViewKey;

@interface MelPopupContentView : NSView
@end

@implementation MelPopupContentView

- (BOOL)isFlipped
{
    return YES;
}

@end

static void npopup_create_backing(Mel_NCtrl* ctrl)
{
    NSRect frame = NSMakeRect(0, 0, (CGFloat)ctrl->size.x, (CGFloat)ctrl->size.y);

    NSPanel* panel = [[NSPanel alloc]
        initWithContentRect:frame
        styleMask:NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel
        backing:NSBackingStoreBuffered
        defer:YES];

    [panel setFloatingPanel:YES];
    [panel setLevel:NSPopUpMenuWindowLevel];
    [panel setHasShadow:YES];
    [panel setOpaque:NO];
    [panel setBackgroundColor:[NSColor windowBackgroundColor]];

    MelPopupContentView* content = [[MelPopupContentView alloc] initWithFrame:frame];
    [panel setContentView:content];

    objc_setAssociatedObject(panel, kPopupContentViewKey, content, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    ctrl->backing = (__bridge_retained void*)panel;
}

static void npopup_destroy_backing(Mel_NCtrl* ctrl)
{
    if (!ctrl->backing) return;

    NSPanel* panel = (__bridge NSPanel*)ctrl->backing;
    [panel orderOut:nil];
    objc_setAssociatedObject(panel, kPopupContentViewKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    NSPanel* transfer = (__bridge_transfer NSPanel*)ctrl->backing;
    (void)transfer;
    ctrl->backing = nullptr;
}

static void npopup_set_frame(Mel_NCtrl* ctrl, f32 x, f32 y, f32 w, f32 h)
{
    (void)x;
    (void)y;
    if (!ctrl->backing) return;

    NSPanel* panel = (__bridge NSPanel*)ctrl->backing;
    NSRect content_rect = NSMakeRect(0, 0, (CGFloat)w, (CGFloat)h);
    NSRect frame_rect = [panel frameRectForContentRect:content_rect];
    frame_rect.origin = panel.frame.origin;
    [panel setFrame:frame_rect display:YES animate:NO];
}

static void npopup_set_visible(Mel_NCtrl* ctrl, bool visible)
{
    if (!ctrl->backing) return;

    NSPanel* panel = (__bridge NSPanel*)ctrl->backing;
    if (visible)
        [panel orderFront:nil];
    else
        [panel orderOut:nil];
}

static void npopup_set_enabled(Mel_NCtrl* ctrl, bool enabled)
{
    (void)ctrl;
    (void)enabled;
}

static Mel_Vec2 npopup_preferred_size(Mel_NCtrl* ctrl)
{
    return ctrl->size;
}

static void npopup_add_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    if (!parent->backing || !child->backing)
        return;

    NSPanel* panel = (__bridge NSPanel*)parent->backing;
    NSView* child_view = (__bridge NSView*)child->backing;
    [[panel contentView] addSubview:child_view];
}

static void npopup_remove_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    if (!child->backing)
        return;

    NSView* child_view = (__bridge NSView*)child->backing;
    [child_view removeFromSuperview];
}

static const Mel_NCtrl_VTable s_npopup_vtable = {
    .create_backing       = npopup_create_backing,
    .destroy_backing      = npopup_destroy_backing,
    .set_frame            = npopup_set_frame,
    .set_visible          = npopup_set_visible,
    .set_enabled          = npopup_set_enabled,
    .preferred_size       = npopup_preferred_size,
    .add_child_backing    = npopup_add_child_backing,
    .remove_child_backing = npopup_remove_child_backing,
};

const Mel_NCtrl_VTable* mel__npopup_vtable(void)
{
    return &s_npopup_vtable;
}

void mel__npopup_show_relative_platform(Mel_NPopup* popup, Mel_NCtrl* anchor)
{
    NSPanel* panel = (__bridge NSPanel*)popup->base.backing;
    if (!panel) return;

    NSView* anchor_view = (__bridge NSView*)anchor->backing;
    if (!anchor_view) return;

    NSRect anchor_screen = [anchor_view.window convertRectToScreen:[anchor_view convertRect:anchor_view.bounds toView:nil]];

    CGFloat popup_w = (CGFloat)popup->base.size.x;
    CGFloat popup_h = (CGFloat)popup->base.size.y;
    CGFloat origin_x = 0;
    CGFloat origin_y = 0;

    switch (popup->side) {
        case MEL_NPOPUP_SIDE_BOTTOM:
            origin_x = NSMinX(anchor_screen);
            origin_y = NSMinY(anchor_screen) - popup_h;
            break;
        case MEL_NPOPUP_SIDE_TOP:
            origin_x = NSMinX(anchor_screen);
            origin_y = NSMaxY(anchor_screen);
            break;
        case MEL_NPOPUP_SIDE_LEFT:
            origin_x = NSMinX(anchor_screen) - popup_w;
            origin_y = NSMinY(anchor_screen);
            break;
        case MEL_NPOPUP_SIDE_RIGHT:
            origin_x = NSMaxX(anchor_screen);
            origin_y = NSMinY(anchor_screen);
            break;
    }

    [panel setFrameOrigin:NSMakePoint(origin_x, origin_y)];
    [panel orderFront:nil];
}

void mel__npopup_close_platform(Mel_NPopup* popup)
{
    NSPanel* panel = (__bridge NSPanel*)popup->base.backing;
    if (!panel) return;
    [panel orderOut:nil];
}
