#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "../ui.native.combo.h"

static const void* kComboTargetKey = &kComboTargetKey;

@interface MelComboTarget : NSObject
@property (nonatomic, assign) Mel_NCombo* combo;
@end

@implementation MelComboTarget

- (void)comboAction:(id)sender
{
    NSPopUpButton* popup = (NSPopUpButton*)sender;
    if (!self.combo)
        return;

    self.combo->selected = (i32)[popup indexOfSelectedItem];

    if (self.combo->on_select)
        self.combo->on_select(self.combo->selected, self.combo->user_data);
}

@end

static void combo_create_backing(Mel_NCtrl* ctrl)
{
    NSRect frame = NSMakeRect(
        (CGFloat)ctrl->pos.x,
        (CGFloat)ctrl->pos.y,
        (CGFloat)ctrl->size.x,
        (CGFloat)ctrl->size.y
    );

    NSPopUpButton* popup = [[NSPopUpButton alloc] initWithFrame:frame pullsDown:NO];

    MelComboTarget* target = [[MelComboTarget alloc] init];
    target.combo = (Mel_NCombo*)ctrl;
    [popup setTarget:target];
    [popup setAction:@selector(comboAction:)];

    objc_setAssociatedObject(popup, kComboTargetKey, target, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    ctrl->backing = (__bridge_retained void*)popup;
}

static void combo_destroy_backing(Mel_NCtrl* ctrl)
{
    if (!ctrl->backing)
        return;

    NSPopUpButton* popup = (__bridge_transfer NSPopUpButton*)ctrl->backing;
    objc_setAssociatedObject(popup, kComboTargetKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    [popup removeFromSuperview];
    (void)popup;
    ctrl->backing = nullptr;
}

static void combo_set_frame(Mel_NCtrl* ctrl, f32 x, f32 y, f32 w, f32 h)
{
    if (!ctrl->backing)
        return;

    NSPopUpButton* popup = (__bridge NSPopUpButton*)ctrl->backing;
    [popup setFrame:NSMakeRect((CGFloat)x, (CGFloat)y, (CGFloat)w, (CGFloat)h)];
}

static void combo_set_visible(Mel_NCtrl* ctrl, bool visible)
{
    if (!ctrl->backing)
        return;

    NSPopUpButton* popup = (__bridge NSPopUpButton*)ctrl->backing;
    [popup setHidden:!visible];
}

static void combo_set_enabled(Mel_NCtrl* ctrl, bool enabled)
{
    if (!ctrl->backing)
        return;

    NSPopUpButton* popup = (__bridge NSPopUpButton*)ctrl->backing;
    [popup setEnabled:enabled];
}

static Mel_Vec2 combo_preferred_size(Mel_NCtrl* ctrl)
{
    if (!ctrl->backing)
        return ctrl->size;

    NSPopUpButton* popup = (__bridge NSPopUpButton*)ctrl->backing;
    NSSize sz = [popup fittingSize];
    return mel_vec2((f32)sz.width, (f32)sz.height);
}

static void combo_add_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    (void)child;
}

static void combo_remove_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    (void)child;
}

static const Mel_NCtrl_VTable s_combo_vtable = {
    .create_backing       = combo_create_backing,
    .destroy_backing      = combo_destroy_backing,
    .set_frame            = combo_set_frame,
    .set_visible          = combo_set_visible,
    .set_enabled          = combo_set_enabled,
    .preferred_size       = combo_preferred_size,
    .add_child_backing    = combo_add_child_backing,
    .remove_child_backing = combo_remove_child_backing,
};

const Mel_NCtrl_VTable* mel__ncombo_vtable_osx(void)
{
    return &s_combo_vtable;
}

void mel__ncombo_set_items_platform(Mel_NCombo* combo, const char** items, i32 count)
{
    assert(combo != nullptr);
    if (!combo->base.backing)
        return;

    NSPopUpButton* popup = (__bridge NSPopUpButton*)combo->base.backing;
    [popup removeAllItems];

    NSMutableArray* titles = [[NSMutableArray alloc] initWithCapacity:(NSUInteger)count];
    for (i32 i = 0; i < count; i++)
        [titles addObject:[NSString stringWithUTF8String:items[i]]];

    [popup addItemsWithTitles:titles];
}

void mel__ncombo_set_selected_platform(Mel_NCombo* combo, i32 index)
{
    assert(combo != nullptr);
    if (!combo->base.backing)
        return;

    NSPopUpButton* popup = (__bridge NSPopUpButton*)combo->base.backing;
    if (index >= 0 && index < (i32)[popup numberOfItems])
        [popup selectItemAtIndex:(NSInteger)index];
}
