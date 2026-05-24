#include "macos.h"

#import <objc/runtime.h>

@implementation MelGuiDialogDelegate

- (BOOL)windowShouldClose:(id)sender
{
    (void)sender;
    if (self.lifecycle.on_close) {
        return self.lifecycle.on_close(self.dialog_handle, mel_gui_user(self.dialog_handle));
    }
    return YES;
}

- (void)windowWillClose:(NSNotification*)note
{
    (void)note;
    Mel_Gui_Handle dialog_h = self.dialog_handle;
    if (mel_gui_handle_is_none(dialog_h)) return;

    Mel_Gui_Node* ow = mel_gui__node(self.owner_handle);
    if (ow && ow->native) {
        NSWindow* owner = (__bridge NSWindow*)ow->native;
        Mel_Gui_Node* dn = mel_gui__node(dialog_h);
        if (dn && dn->native) {
            [owner removeChildWindow:(__bridge NSWindow*)dn->native];
        }
    }

    u32           count = 0;
    Mel_Gui_Node* data  = mel_gui__nodes(&count);
    for (u32 i = 0; i < count; i++) {
        Mel_Gui_Node* cw = &data[i];
        if (!mel_gui_handle_eq(cw->parent, dialog_h)) continue;
        if (!cw->native) continue;
        id obj = (__bridge id)cw->native;
        if ([obj isKindOfClass:[NSView class]]) {
            [(NSView*)obj removeFromSuperview];
        }
        CFBridgingRelease(cw->native);
        cw->native = NULL;
    }

    Mel_Gui_Node* dn = mel_gui__node(dialog_h);
    void* window_native = NULL;
    if (dn && dn->native) {
        window_native = dn->native;
        dn->native    = NULL;
    }

    i32 result = self.result_set ? self.result : 0;
    if (self.on_.on_result) {
        self.on_.on_result(dialog_h, result, mel_gui_user(dialog_h));
    }

    mel_gui__destroy_tree(dialog_h);

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
    mel_gui__resized(self.dialog_handle, (i32)sz.width, (i32)sz.height);
    if (self.lifecycle.on_resize) {
        self.lifecycle.on_resize(self.dialog_handle, (i32)sz.width, (i32)sz.height,
                                 mel_gui_user(self.dialog_handle));
    }
}

- (id)windowWillReturnFieldEditor:(NSWindow*)window toObject:(id)client
{
    return mel_gui__macos_field_editor(window, client);
}

@end

Mel_Gui_Handle mel_dialog_create_opt(Mel_Dialog_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(MEL_GUI_HANDLE_NONE, o.x, o.y, o.w, o.h, 0, o.user,
                                         false, NULL, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    @autoreleasepool {
        i32 cw = n->width  > 0 ? n->width  : 360;
        i32 ch = n->height > 0 ? n->height : 220;

        NSUInteger style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable;
        if (!o.not_resizable) style |= NSWindowStyleMaskResizable;
        if (o.undecorated)    style  = NSWindowStyleMaskBorderless;

        NSRect    content = NSMakeRect(0, 0, cw, ch);
        NSWindow* window  = [[NSWindow alloc] initWithContentRect:content
                                                        styleMask:style
                                                          backing:NSBackingStoreBuffered
                                                            defer:NO];

        MelGuiContentView* root = [[MelGuiContentView alloc] initWithFrame:content];
        root.frame_handle = h;
        [window setContentView:root];

        MelGuiDialogDelegate* delegate = [[MelGuiDialogDelegate alloc] init];
        delegate.dialog_handle = h;
        delegate.owner_handle  = o.owner;
        delegate.lifecycle     = o.lifecycle;
        delegate.on_           = o.on_;
        delegate.focus         = o.focus;
        delegate.keyboard      = o.keyboard;
        [window setDelegate:delegate];
        objc_setAssociatedObject(window, "mel_gui_dialog_delegate", delegate,
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);

        [window setReleasedWhenClosed:NO];
        [window setTitle:mel_gui__macos_nsstring(o.title)];
        [window setLevel:NSFloatingWindowLevel];

        n->native = (void*)CFBridgingRetain(window);
        n->x = 0;
        n->y = 0;

        if (o.min_w > 0 || o.min_h > 0) {
            [window setContentMinSize:NSMakeSize(o.min_w > 0 ? o.min_w : 0,
                                                 o.min_h > 0 ? o.min_h : 0)];
        }
        if (o.max_w > 0 || o.max_h > 0) {
            [window setContentMaxSize:NSMakeSize(o.max_w > 0 ? o.max_w : CGFLOAT_MAX,
                                                 o.max_h > 0 ? o.max_h : CGFLOAT_MAX)];
        }

        Mel_Gui_Node* ow = mel_gui__node(o.owner);
        if (ow && ow->native) {
            NSWindow* owner = (__bridge NSWindow*)ow->native;
            NSRect    of    = owner.frame;
            NSPoint   origin = NSMakePoint(of.origin.x + (of.size.width  - cw) * 0.5,
                                           of.origin.y + (of.size.height - ch) * 0.5);
            [window setFrameOrigin:origin];
            [owner addChildWindow:window ordered:NSWindowAbove];
        } else {
            [window center];
        }

        mel_gui__frames_inc();
        [window makeKeyAndOrderFront:nil];
    }
    return h;
}

void mel_dialog_close(Mel_Gui_Handle dialog, i32 result)
{
    Mel_Gui_Node* n = mel_gui__node(dialog);
    if (!n || !n->native) return;
    NSWindow* window = (__bridge NSWindow*)n->native;
    MelGuiDialogDelegate* delegate =
        objc_getAssociatedObject(window, "mel_gui_dialog_delegate");
    if ([delegate isKindOfClass:[MelGuiDialogDelegate class]]) {
        delegate.result     = result;
        delegate.result_set = YES;
    }
    [window close];
}
