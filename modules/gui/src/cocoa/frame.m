#include "macos.h"

#include <window/window.h>

#import <objc/runtime.h>

@implementation MelGuiWindowDelegate

- (id)windowWillReturnFieldEditor:(NSWindow*)window toObject:(id)client
{
    return mel_gui__macos_field_editor(window, client);
}

@end

static MelGuiWindowDelegate* mel_gui__frame_delegate(Mel_Window mw)
{
    NSWindow* window = (__bridge NSWindow*)mel_window_native(mw);
    if (!window)
        return nil;
    return (MelGuiWindowDelegate*)objc_getAssociatedObject(window, "mel_gui_frame_delegate");
}

static void mel_gui__frame_on_resize(Mel_Window mw, i32 pixel_w, i32 pixel_h, void* user)
{
    (void)pixel_w;
    (void)pixel_h;
    (void)user;
    MelGuiWindowDelegate* d = mel_gui__frame_delegate(mw);
    if (!d)
        return;
    Mel_Gui_Handle h = d.frame_handle;

    i32 pw = 0, ph = 0;
    mel_window_point_extent(mw, &pw, &ph);
    mel_gui__resized(h, pw, ph);
    if (d.lifecycle.on_resize)
        d.lifecycle.on_resize(h, pw, ph, mel_gui_user(h));
}

static void mel_gui__frame_on_focus(Mel_Window mw, void* user)
{
    (void)user;
    NSWindow* window = (__bridge NSWindow*)mel_window_native(mw);
    if (!window)
        return;
    NSResponder* fr = window.firstResponder;
    if (fr == nil || fr == (NSResponder*)window || fr == (NSResponder*)window.contentView)
    {
        [window selectNextKeyView:nil];
    }
}

static void mel_gui__frame_on_closed(Mel_Window mw, void* user)
{
    (void)mw;
    Mel_Gui_Handle frame_h = mel_gui_handle_unpack((u64)(uintptr_t)user);
    if (mel_gui_handle_is_none(frame_h))
        return;

    mel_gui__frame_closed(frame_h);

    Mel_Gui_Node* fw = mel_gui__node(frame_h);
    if (fw)
        fw->native = NULL;

    mel_gui__destroy_tree(frame_h);
}

Mel_Gui_Handle mel_frame_create_opt(Mel_Frame_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(MEL_GUI_HANDLE_NONE, o.x, o.y, o.w, o.h, 0, o.user, o.initial_state == MEL_FRAME_HIDDEN, NULL, o.layout);
    Mel_Gui_Node*  n = mel_gui__node(h);
    if (!n)
        return h;

    @autoreleasepool
    {
        i32 cw = n->width > 0 ? n->width : 480;
        i32 ch = n->height > 0 ? n->height : 360;

        MelGuiContentView* root = [[MelGuiContentView alloc] initWithFrame:NSMakeRect(0, 0, cw, ch)];
        root.frame_handle = h;
        root.inset_mode = o.inset_mode;
        root.insets_cb = o.insets;

        Mel_Window mw = mel_window_create(.title = o.title,
                                          .w = cw,
                                          .h = ch,
                                          .start_hidden = true,
                                          .content_native = (__bridge void*)root,
                                          .user = (void*)(uintptr_t)mel_gui_handle_pack(h),
                                          .lifecycle = { .on_resize = mel_gui__frame_on_resize, .on_closed = mel_gui__frame_on_closed, .on_focus_in = mel_gui__frame_on_focus });

        NSWindow* window = (__bridge NSWindow*)mel_window_native(mw);

        MelGuiWindowDelegate* delegate = [[MelGuiWindowDelegate alloc] init];
        delegate.frame_handle = h;
        delegate.lifecycle = o.lifecycle;
        [window setDelegate:delegate];
        objc_setAssociatedObject(window, "mel_gui_frame_delegate", delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

        n->native = (__bridge void*)window;
        n->x = 0;
        n->y = 0;
    }
    if (mel_style_any(&o.style))
        mel_gui_set_style(h, o.style);
    return h;
}

Mel_Frame_Insets mel_frame_insets(Mel_Gui_Handle h)
{
    Mel_Frame_Insets out = { 0 };
    Mel_Gui_Node*    n = mel_gui__node(mel_gui__toplevel(h));
    if (!n || !n->native)
        return out;

    NSWindow*    window = (__bridge NSWindow*)n->native;
    NSView*      root = window.contentView;
    NSEdgeInsets s = { 0 };
    if (@available(macOS 11.0, *))
        s = root.safeAreaInsets;

    Mel_Insets safe = { (i32)s.left, (i32)s.top, (i32)s.right, (i32)s.bottom };
    out.safe_area = safe;
    out.system_bars = safe;
    return out;
}
