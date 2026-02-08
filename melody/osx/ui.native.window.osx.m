#import <Cocoa/Cocoa.h>
#include "../ui.native.window.h"

@interface MelWindowDelegate : NSObject <NSWindowDelegate>
@property (nonatomic, assign) Mel_NWindow* mel_window;
@end

@implementation MelWindowDelegate

- (void)windowWillClose:(NSNotification*)notification
{
    (void)notification;
    if (self.mel_window && self.mel_window->on_close)
        self.mel_window->on_close(self.mel_window->user_data);
}

- (void)windowDidResize:(NSNotification*)notification
{
    (void)notification;
    if (!self.mel_window)
        return;

    NSWindow* nswindow = (__bridge NSWindow*)self.mel_window->base.backing;
    NSSize content_size = [[nswindow contentView] frame].size;
    self.mel_window->base.size = mel_vec2((f32)content_size.width, (f32)content_size.height);

    Mel_NCtrl* child = self.mel_window->base.first_child;
    while (child) {
        mel_nctrl_set_size(child, self.mel_window->base.size);
        child = child->next_sibling;
    }

    mel_nctrl_perform_layout(&self.mel_window->base);

    if (self.mel_window->on_resize)
        self.mel_window->on_resize((f32)content_size.width, (f32)content_size.height, self.mel_window->user_data);
}

@end

@interface MelFlippedContentView : NSView
@end

@implementation MelFlippedContentView

- (BOOL)isFlipped
{
    return YES;
}

@end

static NSUInteger mel__nwindow_style_to_nsmask(u32 flags)
{
    NSUInteger mask = 0;
    if (flags & MEL_NWINDOW_STYLE_TITLED)
        mask |= NSWindowStyleMaskTitled;
    if (flags & MEL_NWINDOW_STYLE_CLOSABLE)
        mask |= NSWindowStyleMaskClosable;
    if (flags & MEL_NWINDOW_STYLE_MINIATURIZABLE)
        mask |= NSWindowStyleMaskMiniaturizable;
    if (flags & MEL_NWINDOW_STYLE_RESIZABLE)
        mask |= NSWindowStyleMaskResizable;
    return mask;
}

static void window_create_backing(Mel_NCtrl* ctrl)
{
    Mel_NWindow* window = (Mel_NWindow*)ctrl;

    NSUInteger style_mask = mel__nwindow_style_to_nsmask(window->style_flags);
    NSRect content_rect = NSMakeRect(0, 0, (CGFloat)ctrl->size.x, (CGFloat)ctrl->size.y);

    NSWindow* nswindow = [[NSWindow alloc]
        initWithContentRect:content_rect
        styleMask:style_mask
        backing:NSBackingStoreBuffered
        defer:NO];

    [nswindow setTitle:[NSString stringWithUTF8String:window->title]];
    [nswindow center];

    MelWindowDelegate* delegate = [[MelWindowDelegate alloc] init];
    delegate.mel_window = window;
    [nswindow setDelegate:delegate];

    MelFlippedContentView* content_view = [[MelFlippedContentView alloc] initWithFrame:content_rect];
    [nswindow setContentView:content_view];

    ctrl->backing = (__bridge_retained void*)nswindow;
}

static void window_destroy_backing(Mel_NCtrl* ctrl)
{
    if (!ctrl->backing)
        return;

    NSWindow* nswindow = (__bridge_transfer NSWindow*)ctrl->backing;
    [nswindow setDelegate:nil];
    [nswindow close];
    ctrl->backing = nullptr;
}

static void window_set_frame(Mel_NCtrl* ctrl, f32 x, f32 y, f32 w, f32 h)
{
    (void)x;
    (void)y;
    if (!ctrl->backing)
        return;

    NSWindow* nswindow = (__bridge NSWindow*)ctrl->backing;
    NSRect content_rect = NSMakeRect(0, 0, (CGFloat)w, (CGFloat)h);
    NSRect frame_rect = [nswindow frameRectForContentRect:content_rect];
    frame_rect.origin = nswindow.frame.origin;
    [nswindow setFrame:frame_rect display:YES animate:NO];
}

static void window_set_visible(Mel_NCtrl* ctrl, bool visible)
{
    if (!ctrl->backing)
        return;

    NSWindow* nswindow = (__bridge NSWindow*)ctrl->backing;
    if (visible)
        [nswindow orderFront:nil];
    else
        [nswindow orderOut:nil];
}

static void window_set_enabled(Mel_NCtrl* ctrl, bool enabled)
{
    (void)ctrl;
    (void)enabled;
}

static Mel_Vec2 window_preferred_size(Mel_NCtrl* ctrl)
{
    return ctrl->size;
}

static void window_add_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    if (!parent->backing || !child->backing)
        return;

    NSWindow* nswindow = (__bridge NSWindow*)parent->backing;
    NSView* child_view = (__bridge NSView*)child->backing;
    [[nswindow contentView] addSubview:child_view];
}

static void window_remove_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    if (!child->backing)
        return;

    NSView* child_view = (__bridge NSView*)child->backing;
    [child_view removeFromSuperview];
}

static const Mel_NCtrl_VTable s_window_vtable = {
    .create_backing       = window_create_backing,
    .destroy_backing      = window_destroy_backing,
    .set_frame            = window_set_frame,
    .set_visible          = window_set_visible,
    .set_enabled          = window_set_enabled,
    .preferred_size       = window_preferred_size,
    .add_child_backing    = window_add_child_backing,
    .remove_child_backing = window_remove_child_backing,
};

const Mel_NCtrl_VTable* mel__nwindow_vtable(void)
{
    return &s_window_vtable;
}

void mel__nwindow_set_title_platform(Mel_NWindow* window, const char* title)
{
    assert(window != nullptr);
    if (!window->base.backing)
        return;

    NSWindow* nswindow = (__bridge NSWindow*)window->base.backing;
    [nswindow setTitle:[NSString stringWithUTF8String:title]];
}

void mel__nwindow_show_platform(Mel_NWindow* window)
{
    assert(window != nullptr);
    if (!window->base.backing)
        return;

    NSWindow* nswindow = (__bridge NSWindow*)window->base.backing;
    [nswindow makeKeyAndOrderFront:nil];
}

void mel__nwindow_close_platform(Mel_NWindow* window)
{
    assert(window != nullptr);
    if (!window->base.backing)
        return;

    NSWindow* nswindow = (__bridge NSWindow*)window->base.backing;
    [nswindow close];
}
