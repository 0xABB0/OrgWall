#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "../ui.native.menuitem.h"
#include "../ui.native.menu.h"

static const void* kMenuItemTargetKey = &kMenuItemTargetKey;

@interface MelMenuItemTarget : NSObject
@property (nonatomic, assign) Mel_NMenuItem* item;
@end

@implementation MelMenuItemTarget

- (void)menuAction:(id)sender
{
    (void)sender;
    if (_item && _item->on_action)
        _item->on_action(_item->user_data);
}

@end

static void nmenuitem_create_backing(Mel_NCtrl* ctrl)
{
    Mel_NMenuItem* item = (Mel_NMenuItem*)ctrl;

    MelMenuItemTarget* target = [[MelMenuItemTarget alloc] init];
    target.item = item;

    NSString* title = [NSString stringWithUTF8String:item->title];
    NSString* key = [NSString stringWithUTF8String:item->key_equivalent];

    NSMenuItem* ns = [[NSMenuItem alloc]
        initWithTitle:title
        action:@selector(menuAction:)
        keyEquivalent:key];

    [ns setTarget:target];
    [ns setEnabled:YES];

    objc_setAssociatedObject(ns, kMenuItemTargetKey, target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    ctrl->backing = (__bridge_retained void*)ns;
}

static void nmenuitem_destroy_backing(Mel_NCtrl* ctrl)
{
    if (!ctrl->backing) return;

    NSMenuItem* ns = (__bridge NSMenuItem*)ctrl->backing;
    [ns setTarget:nil];
    objc_setAssociatedObject(ns, kMenuItemTargetKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    NSMenuItem* transfer = (__bridge_transfer NSMenuItem*)ctrl->backing;
    (void)transfer;
    ctrl->backing = nullptr;
}

static void nmenuitem_set_frame(Mel_NCtrl* ctrl, f32 x, f32 y, f32 w, f32 h)
{
    (void)ctrl;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

static void nmenuitem_set_visible(Mel_NCtrl* ctrl, bool visible)
{
    if (!ctrl->backing) return;

    NSMenuItem* ns = (__bridge NSMenuItem*)ctrl->backing;
    [ns setHidden:!visible];
}

static void nmenuitem_set_enabled(Mel_NCtrl* ctrl, bool enabled)
{
    if (!ctrl->backing) return;

    NSMenuItem* ns = (__bridge NSMenuItem*)ctrl->backing;
    [ns setEnabled:enabled];
}

static Mel_Vec2 nmenuitem_preferred_size(Mel_NCtrl* ctrl)
{
    (void)ctrl;
    return mel_vec2(0.0f, 0.0f);
}

static void nmenuitem_add_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    (void)child;
}

static void nmenuitem_remove_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    (void)child;
}

static const Mel_NCtrl_VTable s_nmenuitem_vtable = {
    .create_backing       = nmenuitem_create_backing,
    .destroy_backing      = nmenuitem_destroy_backing,
    .set_frame            = nmenuitem_set_frame,
    .set_visible          = nmenuitem_set_visible,
    .set_enabled          = nmenuitem_set_enabled,
    .preferred_size       = nmenuitem_preferred_size,
    .add_child_backing    = nmenuitem_add_child_backing,
    .remove_child_backing = nmenuitem_remove_child_backing,
};

const Mel_NCtrl_VTable* mel__nmenuitem_vtable_osx(void)
{
    return &s_nmenuitem_vtable;
}

void mel__nmenuitem_set_title_platform(Mel_NMenuItem* item, const char* title)
{
    NSMenuItem* ns = (__bridge NSMenuItem*)item->base.backing;
    [ns setTitle:[NSString stringWithUTF8String:title]];
}

void mel__nmenuitem_set_submenu_platform(Mel_NMenuItem* item, Mel_NMenu* submenu)
{
    NSMenuItem* ns = (__bridge NSMenuItem*)item->base.backing;
    if (!ns) return;

    if (submenu && submenu->base.backing) {
        NSMenu* ns_submenu = (__bridge NSMenu*)submenu->base.backing;
        [ns setSubmenu:ns_submenu];
    } else {
        [ns setSubmenu:nil];
    }
}
