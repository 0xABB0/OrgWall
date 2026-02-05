#import <Cocoa/Cocoa.h>
#include "../ui.native.label.h"

static void nlabel_create_backing(Mel_NCtrl* ctrl)
{
    Mel_NLabel* label = (Mel_NLabel*)ctrl;
    NSString* str = [NSString stringWithUTF8String:label->text];
    NSTextField* tf = [NSTextField labelWithString:str];
    [tf setBezeled:NO];
    [tf setDrawsBackground:NO];
    [tf setEditable:NO];
    [tf setSelectable:NO];

    if (label->font_size > 0)
        [tf setFont:[NSFont systemFontOfSize:(CGFloat)label->font_size]];

    ctrl->backing = (__bridge_retained void*)tf;
}

static void nlabel_destroy_backing(Mel_NCtrl* ctrl)
{
    if (!ctrl->backing) return;
    NSTextField* tf = (__bridge_transfer NSTextField*)ctrl->backing;
    (void)tf;
    ctrl->backing = nullptr;
}

static void nlabel_set_frame(Mel_NCtrl* ctrl, f32 x, f32 y, f32 w, f32 h)
{
    NSTextField* tf = (__bridge NSTextField*)ctrl->backing;
    [tf setFrame:NSMakeRect(x, y, w, h)];
}

static void nlabel_set_visible(Mel_NCtrl* ctrl, bool visible)
{
    NSTextField* tf = (__bridge NSTextField*)ctrl->backing;
    [tf setHidden:!visible];
}

static void nlabel_set_enabled(Mel_NCtrl* ctrl, bool enabled)
{
    NSTextField* tf = (__bridge NSTextField*)ctrl->backing;
    [tf setEnabled:enabled];
}

static Mel_Vec2 nlabel_preferred_size(Mel_NCtrl* ctrl)
{
    NSTextField* tf = (__bridge NSTextField*)ctrl->backing;
    NSSize sz = [tf fittingSize];
    return mel_vec2((f32)sz.width, (f32)sz.height);
}

static void nlabel_add_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    (void)child;
}

static void nlabel_remove_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    (void)child;
}

static const Mel_NCtrl_VTable s_nlabel_vtable = {
    .create_backing       = nlabel_create_backing,
    .destroy_backing      = nlabel_destroy_backing,
    .set_frame            = nlabel_set_frame,
    .set_visible          = nlabel_set_visible,
    .set_enabled          = nlabel_set_enabled,
    .preferred_size       = nlabel_preferred_size,
    .add_child_backing    = nlabel_add_child_backing,
    .remove_child_backing = nlabel_remove_child_backing,
};

const Mel_NCtrl_VTable* mel__nlabel_vtable_osx(void)
{
    return &s_nlabel_vtable;
}

void mel__nlabel_set_text_platform(Mel_NLabel* label, const char* text)
{
    NSTextField* tf = (__bridge NSTextField*)label->base.backing;
    [tf setStringValue:[NSString stringWithUTF8String:text]];
}

void mel__nlabel_set_font_size_platform(Mel_NLabel* label, f32 size)
{
    NSTextField* tf = (__bridge NSTextField*)label->base.backing;
    [tf setFont:[NSFont systemFontOfSize:(CGFloat)size]];
}
