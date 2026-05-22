#include "macos.h"

#import <objc/runtime.h>

@implementation MelGuiWindowDelegate

- (void)windowWillClose:(NSNotification*)note
{
    (void)note;
    Mel_Gui_Handle frame_h = self.frame_handle;
    if (mel_gui_handle_is_none(frame_h)) return;

    u32             count = 0;
    Mel_Gui_Widget* data  = mel_gui__widgets(&count);
    for (u32 i = 0; i < count; i++) {
        Mel_Gui_Widget* cw = &data[i];
        if (!mel_gui_handle_eq(cw->parent, frame_h)) continue;
        if (!cw->native) continue;
        id obj = (__bridge id)cw->native;
        if ([obj isKindOfClass:[NSView class]]) {
            [(NSView*)obj removeFromSuperview];
        }
        CFBridgingRelease(cw->native);
        cw->native = NULL;
    }

    Mel_Gui_Widget* fw = mel_gui__get(frame_h);
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
    mel_gui__fire_resize(self.frame_handle, (i32)sz.width, (i32)sz.height);
}

- (void)windowDidBecomeKey:(NSNotification*)note
{
    NSWindow* window = (NSWindow*)note.object;
    NSResponder* fr  = window.firstResponder;
    if (fr == nil || fr == (NSResponder*)window || fr == (NSResponder*)window.contentView) {
        [window selectNextKeyView:nil];
    }
}

@end

void mel_gui__backend_frame_create(Mel_Gui_Widget* w, str8 title)
{
    @autoreleasepool {
        i32 cw = w->width  > 0 ? w->width  : 480;
        i32 ch = w->height > 0 ? w->height : 360;

        NSUInteger style = NSWindowStyleMaskTitled
                         | NSWindowStyleMaskClosable
                         | NSWindowStyleMaskMiniaturizable
                         | NSWindowStyleMaskResizable;

        NSRect content = NSMakeRect(0, 0, cw, ch);
        NSWindow* window = [[NSWindow alloc] initWithContentRect:content
                                                       styleMask:style
                                                         backing:NSBackingStoreBuffered
                                                           defer:NO];

        MelGuiContentView* root = [[MelGuiContentView alloc] initWithFrame:content];
        root.frame_handle = w->self;
        [window setContentView:root];

        MelGuiWindowDelegate* delegate = [[MelGuiWindowDelegate alloc] init];
        delegate.frame_handle = w->self;
        [window setDelegate:delegate];
        objc_setAssociatedObject(window, "mel_gui_delegate", delegate,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);

        [window setReleasedWhenClosed:NO];
        [window setTitle:mel_gui__macos_nsstring(title)];
        [window center];

        w->native = (void*)CFBridgingRetain(window);
        w->x = 0;
        w->y = 0;
        mel_gui__frames_inc();
    }
}
