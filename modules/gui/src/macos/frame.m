#include "macos.h"

#import <objc/runtime.h>

@implementation MelGuiWindowDelegate

- (void)windowWillClose:(NSNotification*)note
{
    (void)note;
    Mel_Gui_Handle frame_h = self.frame_handle;
    if (mel_gui_handle_is_none(frame_h)) return;

    u32           count = 0;
    Mel_Gui_Node* data  = mel_gui__nodes(&count);
    for (u32 i = 0; i < count; i++) {
        Mel_Gui_Node* cw = &data[i];
        if (!mel_gui_handle_eq(cw->parent, frame_h)) continue;
        if (!cw->native) continue;
        id obj = (__bridge id)cw->native;
        if ([obj isKindOfClass:[NSView class]]) {
            [(NSView*)obj removeFromSuperview];
        }
        CFBridgingRelease(cw->native);
        cw->native = NULL;
    }

    Mel_Gui_Node* fw = mel_gui__node(frame_h);
    void* window_native = NULL;
    if (fw && fw->native) {
        window_native = fw->native;
        fw->native    = NULL;
    }

    mel_gui__destroy_tree(frame_h);

    if (window_native) {
        dispatch_async(dispatch_get_main_queue(), ^{
            CFBridgingRelease(window_native);
        });
    }

    if (mel_gui__frames_dec() == 0) {
        Mel_Reactor* r = mel_gui__reactor();
        if (r) mel_reactor_quit(r);
    }
}

- (void)windowDidResize:(NSNotification*)note
{
    NSWindow* window = (NSWindow*)note.object;
    NSSize    sz     = window.contentView.bounds.size;
    mel_gui__resized(self.frame_handle, (i32)sz.width, (i32)sz.height);
    if (self.lifecycle.on_resize) {
        self.lifecycle.on_resize(self.frame_handle, (i32)sz.width, (i32)sz.height,
                                 mel_gui_user(self.frame_handle));
    }
}

- (void)windowDidBecomeKey:(NSNotification*)note
{
    NSWindow* window = (NSWindow*)note.object;
    NSResponder* fr  = window.firstResponder;
    if (fr == nil || fr == (NSResponder*)window || fr == (NSResponder*)window.contentView) {
        [window selectNextKeyView:nil];
    }
}

- (id)windowWillReturnFieldEditor:(NSWindow*)window toObject:(id)client
{
    return mel_gui__macos_field_editor(window, client);
}

@end

Mel_Gui_Handle mel_frame_create_opt(Mel_Frame_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(MEL_GUI_HANDLE_NONE, o.x, o.y, o.w, o.h, 0, o.user,
                                         o.initial_state == MEL_FRAME_HIDDEN, NULL, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    @autoreleasepool {
        i32 cw = n->width  > 0 ? n->width  : 480;
        i32 ch = n->height > 0 ? n->height : 360;

        NSUInteger style = NSWindowStyleMaskTitled
                         | NSWindowStyleMaskClosable
                         | NSWindowStyleMaskMiniaturizable
                         | NSWindowStyleMaskResizable;

        NSRect    content = NSMakeRect(0, 0, cw, ch);
        NSWindow* window  = [[NSWindow alloc] initWithContentRect:content
                                                        styleMask:style
                                                          backing:NSBackingStoreBuffered
                                                            defer:NO];

        MelGuiContentView* root = [[MelGuiContentView alloc] initWithFrame:content];
        root.frame_handle = h;
        [window setContentView:root];

        MelGuiWindowDelegate* delegate = [[MelGuiWindowDelegate alloc] init];
        delegate.frame_handle = h;
        delegate.lifecycle    = o.lifecycle;
        [window setDelegate:delegate];
        objc_setAssociatedObject(window, "mel_gui_delegate", delegate,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);

        [window setReleasedWhenClosed:NO];
        [window setTitle:mel_gui__macos_nsstring(o.title)];
        [window center];

        n->native = (void*)CFBridgingRetain(window);
        n->x = 0;
        n->y = 0;
        mel_gui__frames_inc();
    }
    return h;
}
