#import <Cocoa/Cocoa.h>
#import <objc/runtime.h>
#include "../ui.native.textview.h"

static const void* kTextViewDelegateKey = &kTextViewDelegateKey;
static const void* kTextViewKey = &kTextViewKey;

@interface MelTextViewDelegate : NSObject <NSTextViewDelegate>
@property (nonatomic, assign) Mel_NTextView* tv;
@end

@implementation MelTextViewDelegate

- (void)textDidChange:(NSNotification*)notification
{
    (void)notification;
    if (_tv && _tv->on_change)
        _tv->on_change(_tv->user_data);
}

@end

static NSTextView* mel__ntextview_get_nstextview(Mel_NTextView* tv)
{
    NSScrollView* scroll = (__bridge NSScrollView*)tv->base.backing;
    return (NSTextView*)objc_getAssociatedObject(scroll, kTextViewKey);
}

static void ntextview_create_backing(Mel_NCtrl* ctrl)
{
    Mel_NTextView* tv = (Mel_NTextView*)ctrl;

    NSRect frame = NSMakeRect(0, 0, 300, 200);

    NSTextView* text_view = [[NSTextView alloc] initWithFrame:frame];
    [text_view setEditable:tv->editable];
    [text_view setRichText:NO];
    [text_view setAutoresizingMask:NSViewWidthSizable | NSViewHeightSizable];
    [[text_view textContainer] setWidthTracksTextView:YES];

    MelTextViewDelegate* delegate = [[MelTextViewDelegate alloc] init];
    delegate.tv = tv;
    [text_view setDelegate:delegate];

    NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:frame];
    [scroll setHasVerticalScroller:YES];
    [scroll setHasHorizontalScroller:NO];
    [scroll setBorderType:NSBezelBorder];
    [scroll setDocumentView:text_view];

    objc_setAssociatedObject(scroll, kTextViewDelegateKey, delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(scroll, kTextViewKey, text_view, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    ctrl->backing = (__bridge_retained void*)scroll;
}

static void ntextview_destroy_backing(Mel_NCtrl* ctrl)
{
    if (!ctrl->backing) return;

    NSScrollView* scroll = (__bridge NSScrollView*)ctrl->backing;
    NSTextView* text_view = (NSTextView*)objc_getAssociatedObject(scroll, kTextViewKey);
    [text_view setDelegate:nil];

    objc_setAssociatedObject(scroll, kTextViewDelegateKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);
    objc_setAssociatedObject(scroll, kTextViewKey, nil, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

    NSScrollView* transfer = (__bridge_transfer NSScrollView*)ctrl->backing;
    (void)transfer;
    ctrl->backing = nullptr;
}

static void ntextview_set_frame(Mel_NCtrl* ctrl, f32 x, f32 y, f32 w, f32 h)
{
    NSScrollView* scroll = (__bridge NSScrollView*)ctrl->backing;
    [scroll setFrame:NSMakeRect(x, y, w, h)];
}

static void ntextview_set_visible(Mel_NCtrl* ctrl, bool visible)
{
    NSScrollView* scroll = (__bridge NSScrollView*)ctrl->backing;
    [scroll setHidden:!visible];
}

static void ntextview_set_enabled(Mel_NCtrl* ctrl, bool enabled)
{
    Mel_NTextView* tv = (Mel_NTextView*)ctrl;
    NSTextView* text_view = mel__ntextview_get_nstextview(tv);
    [text_view setEditable:enabled && tv->editable];
    [text_view setSelectable:enabled];
}

static Mel_Vec2 ntextview_preferred_size(Mel_NCtrl* ctrl)
{
    (void)ctrl;
    return mel_vec2(300.0f, 200.0f);
}

static void ntextview_add_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    (void)child;
}

static void ntextview_remove_child_backing(Mel_NCtrl* parent, Mel_NCtrl* child)
{
    (void)parent;
    (void)child;
}

static const Mel_NCtrl_VTable s_ntextview_vtable = {
    .create_backing       = ntextview_create_backing,
    .destroy_backing      = ntextview_destroy_backing,
    .set_frame            = ntextview_set_frame,
    .set_visible          = ntextview_set_visible,
    .set_enabled          = ntextview_set_enabled,
    .preferred_size       = ntextview_preferred_size,
    .add_child_backing    = ntextview_add_child_backing,
    .remove_child_backing = ntextview_remove_child_backing,
};

const Mel_NCtrl_VTable* mel__ntextview_vtable_osx(void)
{
    return &s_ntextview_vtable;
}

void mel__ntextview_set_text_platform(Mel_NTextView* tv, const char* text)
{
    NSTextView* text_view = mel__ntextview_get_nstextview(tv);
    [text_view setString:[NSString stringWithUTF8String:text]];
}

const char* mel__ntextview_get_text_platform(Mel_NTextView* tv)
{
    NSTextView* text_view = mel__ntextview_get_nstextview(tv);
    return [[text_view string] UTF8String];
}

void mel__ntextview_set_editable_platform(Mel_NTextView* tv, bool editable)
{
    NSTextView* text_view = mel__ntextview_get_nstextview(tv);
    [text_view setEditable:editable];
}
