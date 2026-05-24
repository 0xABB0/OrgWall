#include "macos.h"

Mel_Gui_Handle mel_scrollview_create_opt(Mel_Gui_Handle parent, Mel_ScrollView_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    @autoreleasepool {
        NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:NSMakeRect(0, 0, n->width, n->height)];

        i32 cw = o.content_w > 0 ? o.content_w : n->width;
        i32 ch = o.content_h > 0 ? o.content_h : n->height;

        scroll.hasVerticalScroller   = (ch > n->height);
        scroll.hasHorizontalScroller = (cw > n->width);
        scroll.autohidesScrollers    = YES;
        scroll.borderType            = NSNoBorder;
        scroll.drawsBackground       = NO;

        MelGuiContainerView* doc = [[MelGuiContainerView alloc] initWithFrame:NSMakeRect(0, 0, cw, ch)];
        doc.handle = h;
        doc.focus  = o.focus;
        scroll.documentView = doc;

        mel_gui__macos_install_child(n, scroll);
        n->content = (__bridge void*)doc;
    }
    return h;
}
