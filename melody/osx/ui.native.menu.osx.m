#import <Cocoa/Cocoa.h>
#include "../ui.native.menu.h"

static void nmenu_create_backing(Mel_NCtrl* ctrl)
{
    Mel_NMenu* menu = (Mel_NMenu*)ctrl;

    NSMenu* ns = [[NSMenu alloc] initWithTitle:[NSString stringWithUTF8String:menu->title]];
    [ns setAutoenablesItems:NO];

    ctrl->backing = (__bridge_retained void*)ns;
}

static void nmenu_destroy_backing(Mel_NCtrl* ctrl)
{
    if (!ctrl->backing) return;

    NSMenu* ns = (__bridge_transfer NSMenu*)ctrl->backing;
    [ns removeAllItems];
    (void)ns;
    ctrl->backing = nullptr;
}

static void nmenu_set_frame(Mel_NCtrl* ctrl, f32 x, f32 y, f32 w, f32 h)
{
    (void)ctrl;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

static void nmenu_set_visible(Mel_NCtrl* ctrl, bool visible)
{
    (void)ctrl;
    (void)visible;
}

static void nmenu_set_enabled(Mel_NCtrl* ctrl, bool enabled)
{
    (void)ctrl;
    (void)enabled;
}

static Mel_Vec2 nmenu_preferred_size(Mel_NCtrl* ctrl)
{
    (void)ctrl;
    return mel_vec2(0.0f, 0.0f);
}

static void nmenu_add_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    if (!parent->backing || !child->backing)
        return;

    NSMenu* ns_menu = (__bridge NSMenu*)parent->backing;
    NSMenuItem* ns_item = (__bridge NSMenuItem*)child->backing;
    [ns_menu addItem:ns_item];
}

static void nmenu_remove_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    if (!parent->backing || !child->backing)
        return;

    NSMenu* ns_menu = (__bridge NSMenu*)parent->backing;
    NSMenuItem* ns_item = (__bridge NSMenuItem*)child->backing;
    [ns_menu removeItem:ns_item];
}

static const Mel_NCtrl_VTable s_nmenu_vtable = {
    .create_backing       = nmenu_create_backing,
    .destroy_backing      = nmenu_destroy_backing,
    .set_frame            = nmenu_set_frame,
    .set_visible          = nmenu_set_visible,
    .set_enabled          = nmenu_set_enabled,
    .preferred_size       = nmenu_preferred_size,
    .add_child_backing    = nmenu_add_child_backing,
    .remove_child_backing = nmenu_remove_child_backing,
};

const Mel_NCtrl_VTable* mel__nmenu_vtable(void)
{
    return &s_nmenu_vtable;
}

void mel__nmenu_set_title_platform(Mel_NMenu* menu, const char* title)
{
    NSMenu* ns = (__bridge NSMenu*)menu->base.backing;
    [ns setTitle:[NSString stringWithUTF8String:title]];
}

void mel__nmenu_popup_platform(Mel_NMenu* menu, Mel_Vec2 location, Mel_NCtrl* in_view)
{
    NSMenu* ns_menu = (__bridge NSMenu*)menu->base.backing;
    if (!ns_menu) return;

    NSView* view = (__bridge NSView*)in_view->backing;
    if (!view) return;

    [NSMenu popUpContextMenu:ns_menu withEvent:[NSApp currentEvent] forView:view];
}
