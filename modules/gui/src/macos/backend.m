#include "macos.h"
#include <gui/appkit/frame.h>

NSWindow* mel_gui_appkit_nswindow(Mel_Gui_Handle h)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (!w || !w->native) return nil;
    id obj = (__bridge id)w->native;
    return [obj isKindOfClass:[NSWindow class]] ? (NSWindow*)obj : nil;
}

NSView* mel_gui_appkit_nsview(Mel_Gui_Handle h)
{
    Mel_Gui_Widget* w = mel_gui__get(h);
    if (!w || !w->native) return nil;
    id obj = (__bridge id)w->native;
    if ([obj isKindOfClass:[NSView   class]]) return (NSView*)obj;
    if ([obj isKindOfClass:[NSWindow class]]) return [(NSWindow*)obj contentView];
    return nil;
}

@implementation MelGuiContentView
- (BOOL)isFlipped { return YES; }
@end

@implementation MelGuiTextFieldDelegate
- (void)controlTextDidChange:(NSNotification*)note
{
    Mel_Gui_Widget* w = mel_gui__get(self.handle);
    if (!w) return;
    Mel_Gui_TextField_Impl* impl = (Mel_Gui_TextField_Impl*)w->impl;
    if (!impl || !impl->on_.on_text_changed) return;

    NSTextField* tf = (NSTextField*)note.object;
    NSString*    s  = tf.stringValue;
    const char*  c  = [s UTF8String];
    size         n  = c ? (size)strlen(c) : 0;
    str8         t  = { (u8*)c, n };
    impl->on_.on_text_changed(self.handle, t, w->user);
}
- (void)controlTextDidBeginEditing:(NSNotification*)note
{
    (void)note;
    mel_gui__set_focused(self.handle);
    mel_gui__macos_fire_focus_in(self.handle);
}
- (void)controlTextDidEndEditing:(NSNotification*)note
{
    (void)note;
    if (mel_gui_handle_eq(mel_gui_focused(), self.handle)) {
        mel_gui__set_focused(MEL_GUI_HANDLE_NONE);
    }
    mel_gui__macos_fire_focus_out(self.handle);
}
@end

NSString* mel_gui__macos_nsstring(str8 s)
{
    if (s.len <= 0 || s.data == NULL) return @"";
    return [[NSString alloc] initWithBytes:s.data
                                    length:(NSUInteger)s.len
                                  encoding:NSUTF8StringEncoding] ?: @"";
}

NSView* mel_gui__macos_parent_view(Mel_Gui_Widget* w)
{
    Mel_Gui_Widget* p = mel_gui__get(w->parent);
    if (!p || !p->native) return nil;
    id obj = (__bridge id)p->native;
    if ([obj isKindOfClass:[NSWindow class]]) return [(NSWindow*)obj contentView];
    if ([obj isKindOfClass:[NSView   class]]) return (NSView*)obj;
    return nil;
}

void mel_gui__macos_install_child(Mel_Gui_Widget* w, NSView* view)
{
    NSView* parent = mel_gui__macos_parent_view(w);
    if (!parent) return;
    [view setFrame:NSMakeRect(w->x, w->y, w->width, w->height)];
    view.hidden = w->hidden;
    [parent addSubview:view];
    w->native = (void*)CFBridgingRetain(view);
}

void mel_gui__macos_fire_focus_in(Mel_Gui_Handle h)
{
    mel_gui__fire_focus_in(h);
}

void mel_gui__macos_fire_focus_out(Mel_Gui_Handle h)
{
    mel_gui__fire_focus_out(h);
}

Mel_Key mel_gui__macos_key_for_event(NSEvent* e)
{
    unsigned short kc = e.keyCode;
    switch (kc) {
        case 0x33: return MEL_KEY_BACKSPACE;
        case 0x30: return MEL_KEY_TAB;
        case 0x24: return MEL_KEY_ENTER;
        case 0x4C: return MEL_KEY_ENTER;
        case 0x35: return MEL_KEY_ESCAPE;
        case 0x31: return MEL_KEY_SPACE;
        case 0x7B: return MEL_KEY_LEFT;
        case 0x7C: return MEL_KEY_RIGHT;
        case 0x7E: return MEL_KEY_UP;
        case 0x7D: return MEL_KEY_DOWN;
        case 0x73: return MEL_KEY_HOME;
        case 0x77: return MEL_KEY_END;
        case 0x74: return MEL_KEY_PAGE_UP;
        case 0x79: return MEL_KEY_PAGE_DOWN;
        case 0x72: return MEL_KEY_INSERT;
        case 0x75: return MEL_KEY_DELETE;
        default: break;
    }
    NSString* chars = e.charactersIgnoringModifiers;
    if (chars.length == 0) return MEL_KEY_NONE;
    unichar c = [chars characterAtIndex:0];
    if (c >= '0' && c <= '9') return (Mel_Key)c;
    if (c >= 'a' && c <= 'z') return (Mel_Key)(c - 'a' + 'A');
    if (c >= 'A' && c <= 'Z') return (Mel_Key)c;
    return MEL_KEY_NONE;
}

static void install_default_menu(void)
{
    NSMenu*     bar      = [[NSMenu alloc] init];
    NSMenuItem* app_item = [[NSMenuItem alloc] init];
    [bar addItem:app_item];
    [NSApp setMainMenu:bar];

    NSMenu* app_menu = [[NSMenu alloc] init];
    [app_menu addItemWithTitle:@"Quit"
                        action:@selector(terminate:)
                 keyEquivalent:@"q"];
    [app_item setSubmenu:app_menu];
}

bool mel_gui__backend_init(void)
{
    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        install_default_menu();
        [NSApp finishLaunching];
    }
    return true;
}

void mel_gui__backend_destroy(Mel_Gui_Widget* w)
{
    if (!w || !w->native) return;
    id obj = (__bridge id)w->native;

    if ([obj isKindOfClass:[NSWindow class]]) {
        [(NSWindow*)obj close];
        return;
    }

    if ([obj isKindOfClass:[NSView class]]) {
        [(NSView*)obj removeFromSuperview];
    }

    CFBridgingRelease(w->native);
    w->native = NULL;
}

void mel_gui__backend_set_text(Mel_Gui_Widget* w, str8 text)
{
    if (!w || !w->native) return;
    NSString* s   = mel_gui__macos_nsstring(text);
    id        obj = (__bridge id)w->native;

    if ([obj isKindOfClass:[NSWindow class]]) {
        [(NSWindow*)obj setTitle:s];
    } else if ([obj isKindOfClass:[NSTextField class]]) {
        [(NSTextField*)obj setStringValue:s];
    } else if ([obj isKindOfClass:[NSButton class]]) {
        [(NSButton*)obj setTitle:s];
    }
}

size mel_gui__backend_get_text(Mel_Gui_Widget* w, char* buf, size cap)
{
    if (buf && cap > 0) buf[0] = 0;
    if (!w || !w->native || !buf || cap <= 0) return 0;

    NSString* s = nil;
    id        obj = (__bridge id)w->native;

    if ([obj isKindOfClass:[NSWindow class]]) {
        s = [(NSWindow*)obj title];
    } else if ([obj isKindOfClass:[NSTextField class]]) {
        s = [(NSTextField*)obj stringValue];
    } else if ([obj isKindOfClass:[NSButton class]]) {
        s = [(NSButton*)obj title];
    }
    if (!s) return 0;

    const char* c = [s UTF8String];
    if (!c) { buf[0] = 0; return 0; }

    size n = (size)strlen(c);
    if (n > cap - 1) n = cap - 1;
    memcpy(buf, c, (usize)n);
    buf[n] = 0;
    return n;
}

void mel_gui__backend_set_bounds(Mel_Gui_Widget* w, i32 x, i32 y, i32 width, i32 height)
{
    if (!w || !w->native) return;
    id obj = (__bridge id)w->native;

    if ([obj isKindOfClass:[NSWindow class]]) {
        NSWindow* window = (NSWindow*)obj;
        NSRect    cur    = window.frame;
        NSRect    desired_content = NSMakeRect(0, 0, width, height);
        NSRect    desired_frame   = [window frameRectForContentRect:desired_content];

        bool reposition = (x != 0 || y != 0);
        if (reposition) {
            NSScreen* screen = window.screen ?: [NSScreen mainScreen];
            CGFloat   sh     = screen.frame.size.height;
            NSPoint   topLeft = NSMakePoint((CGFloat)x, sh - (CGFloat)y);
            NSRect    fr = desired_frame;
            fr.origin = NSMakePoint(topLeft.x, topLeft.y - fr.size.height);
            [window setFrame:fr display:YES];
        } else {
            CGFloat top = cur.origin.y + cur.size.height;
            NSRect  fr  = desired_frame;
            fr.origin   = NSMakePoint(cur.origin.x, top - fr.size.height);
            [window setFrame:fr display:YES];
        }
    } else if ([obj isKindOfClass:[NSView class]]) {
        NSView* view = (NSView*)obj;
        [view setFrame:NSMakeRect(x, y, width, height)];
        [view setNeedsDisplay:YES];
    }
}

void mel_gui__backend_set_visible(Mel_Gui_Widget* w, bool visible)
{
    if (!w || !w->native) return;
    id obj = (__bridge id)w->native;

    if ([obj isKindOfClass:[NSWindow class]]) {
        NSWindow* window = (NSWindow*)obj;
        if (visible) {
            [window makeKeyAndOrderFront:nil];
        } else {
            [window orderOut:nil];
        }
    } else if ([obj isKindOfClass:[NSView class]]) {
        [(NSView*)obj setHidden:!visible];
    }
}

void mel_gui__backend_set_enabled(Mel_Gui_Widget* w, bool enabled)
{
    if (!w || !w->native) return;
    id obj = (__bridge id)w->native;
    if ([obj isKindOfClass:[NSControl class]]) {
        [(NSControl*)obj setEnabled:enabled];
    }
}

void mel_gui__backend_set_focus(Mel_Gui_Widget* w)
{
    if (!w || !w->native) return;
    id obj = (__bridge id)w->native;

    if ([obj isKindOfClass:[NSWindow class]]) {
        NSWindow* window = (NSWindow*)obj;
        [window makeKeyAndOrderFront:nil];
        [NSApp activateIgnoringOtherApps:YES];
    } else if ([obj isKindOfClass:[NSView class]]) {
        NSView* view = (NSView*)obj;
        [view.window makeFirstResponder:view];
    }
}

void mel_gui__backend_invalidate(Mel_Gui_Widget* w)
{
    if (!w || !w->native) return;
    id obj = (__bridge id)w->native;
    if ([obj isKindOfClass:[NSView class]]) {
        [(NSView*)obj setNeedsDisplay:YES];
    } else if ([obj isKindOfClass:[NSWindow class]]) {
        [[(NSWindow*)obj contentView] setNeedsDisplay:YES];
    }
}
