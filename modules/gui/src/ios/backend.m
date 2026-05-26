#include "uikit.h"

#include <string.h>

void mel_gui__ios_sync(dispatch_block_t block)
{
    if ([NSThread isMainThread]) block();
    else dispatch_sync(dispatch_get_main_queue(), block);
}

NSString* mel_gui__ios_nsstring(str8 s)
{
    if (s.len <= 0 || s.data == NULL) return @"";
    return [[NSString alloc] initWithBytes:s.data
                                    length:(NSUInteger)s.len
                                  encoding:NSUTF8StringEncoding] ?: @"";
}

UIView* mel_gui__ios_parent_view(Mel_Gui_Node* n)
{
    Mel_Gui_Node* p = mel_gui__node(n->parent);
    if (!p) return nil;
    if (p->content) return (__bridge UIView*)p->content;
    if (!p->native) return nil;
    id obj = (__bridge id)p->native;
    if ([obj isKindOfClass:[UIWindow class]]) return ((UIWindow*)obj).rootViewController.view;
    if ([obj isKindOfClass:[UIView class]])   return (UIView*)obj;
    return nil;
}

void mel_gui__ios_install_child(Mel_Gui_Node* n, UIView* view)
{
    __block UIView* parent = nil;
    mel_gui__ios_sync(^{
        parent = mel_gui__ios_parent_view(n);
        if (!parent) return;
        view.frame  = CGRectMake(n->x, n->y, n->width, n->height);
        view.hidden = n->hidden;
        [parent addSubview:view];
    });
    if (parent) n->native = (void*)CFBridgingRetain(view);
}

bool mel_gui__backend_init(void)
{
    // UIApplicationMain (modules/app/src/ios) already stood up UIApplication.
    return true;
}

void mel_gui__backend_destroy(Mel_Gui_Node* n)
{
    if (!n || !n->native) return;
    void* native  = n->native;
    void* content = n->content;
    n->native  = NULL;
    n->content = NULL;
    mel_gui__ios_sync(^{
        id obj = (__bridge id)native;
        if ([obj isKindOfClass:[UIWindow class]]) {
            ((UIWindow*)obj).hidden = YES;
        } else if ([obj isKindOfClass:[UIView class]]) {
            [(UIView*)obj removeFromSuperview];
        }
        CFBridgingRelease(native);
        if (content) CFBridgingRelease(content);
    });
}

void mel_gui_set_text(Mel_Gui_Handle h, str8 text)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native) return;
    NSString* s = mel_gui__ios_nsstring(text);
    mel_gui__ios_sync(^{
        id obj = (__bridge id)n->native;
        if ([obj isKindOfClass:[UILabel class]])          [(UILabel*)obj setText:s];
        else if ([obj isKindOfClass:[UITextField class]]) [(UITextField*)obj setText:s];
        else if ([obj isKindOfClass:[UIButton class]])    [(UIButton*)obj setTitle:s forState:UIControlStateNormal];
    });
}

size mel_gui_get_text(Mel_Gui_Handle h, char* buf, size cap)
{
    if (buf && cap > 0) buf[0] = 0;
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native || !buf || cap <= 0) return 0;

    __block NSString* s = nil;
    mel_gui__ios_sync(^{
        id obj = (__bridge id)n->native;
        if ([obj isKindOfClass:[UILabel class]])          s = [(UILabel*)obj text];
        else if ([obj isKindOfClass:[UITextField class]]) s = [(UITextField*)obj text];
        else if ([obj isKindOfClass:[UIButton class]])    s = [(UIButton*)obj titleForState:UIControlStateNormal];
    });
    if (!s) return 0;
    const char* c = [s UTF8String];
    if (!c) { buf[0] = 0; return 0; }
    size m = (size)strlen(c);
    if (m > cap - 1) m = cap - 1;
    memcpy(buf, c, (usize)m);
    buf[m] = 0;
    return m;
}

void mel_gui_set_bounds(Mel_Gui_Handle h, i32 x, i32 y, i32 width, i32 height)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return;
    n->x = x; n->y = y; n->width = width; n->height = height;
    if (!n->native) return;
    mel_gui__ios_sync(^{
        id obj = (__bridge id)n->native;
        // The window stays full-screen; only embedded views follow layout.
        if ([obj isKindOfClass:[UIView class]] && ![obj isKindOfClass:[UIWindow class]]) {
            [(UIView*)obj setFrame:CGRectMake(x, y, width, height)];
            [(UIView*)obj setNeedsDisplay];
        }
    });
}

void mel_gui_set_visible(Mel_Gui_Handle h, bool visible)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return;
    n->hidden = !visible;
    if (!n->native) return;
    mel_gui__ios_sync(^{
        id obj = (__bridge id)n->native;
        if ([obj isKindOfClass:[UIWindow class]]) {
            UIWindow* w = (UIWindow*)obj;
            if (visible) [w makeKeyAndVisible];
            else         w.hidden = YES;
        } else if ([obj isKindOfClass:[UIView class]]) {
            [(UIView*)obj setHidden:!visible];
        }
    });
}

void mel_gui_set_enabled(Mel_Gui_Handle h, bool enabled)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native) return;
    mel_gui__ios_sync(^{
        id obj = (__bridge id)n->native;
        if ([obj isKindOfClass:[UIControl class]]) [(UIControl*)obj setEnabled:enabled];
    });
}

void mel_gui_set_focus(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native) return;
    mel_gui__ios_sync(^{
        id obj = (__bridge id)n->native;
        if ([obj isKindOfClass:[UIWindow class]])    [(UIWindow*)obj makeKeyAndVisible];
        else if ([obj isKindOfClass:[UIView class]]) [(UIView*)obj becomeFirstResponder];
    });
    mel_gui__set_focused(h);
}

void mel_gui_invalidate(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native) return;
    mel_gui__ios_sync(^{
        id obj = (__bridge id)n->native;
        if ([obj isKindOfClass:[UIView class]]) [(UIView*)obj setNeedsDisplay];
    });
}

void mel_gui__nav_replace(Mel_Gui_Handle next, Mel_Gui_Handle prev)
{
    mel_gui_set_visible(next, true);
    mel_gui_set_focus(next);
    if (!mel_gui_handle_is_none(prev)) mel_gui_set_visible(prev, false);
}

void mel_gui__nav_back(Mel_Gui_Handle prev, Mel_Gui_Handle cur)
{
    mel_gui_set_visible(prev, true);
    mel_gui_set_focus(prev);
    if (!mel_gui_handle_is_none(cur)) mel_gui_set_visible(cur, false);
}

Mel_Frame_Insets mel_frame_insets(Mel_Gui_Handle h)
{
    __block Mel_Frame_Insets out = {0};
    Mel_Gui_Node*    n   = mel_gui__node(mel_gui__toplevel(h));
    if (!n || !n->native) return out;
    mel_gui__ios_sync(^{
        UIWindow* w = (__bridge UIWindow*)n->native;
        UIEdgeInsets s = w.safeAreaInsets;
        Mel_Insets safe = { (i32)s.left, (i32)s.top, (i32)s.right, (i32)s.bottom };
        out.safe_area   = safe;
        out.system_bars = safe;
    });
    return out;
}
