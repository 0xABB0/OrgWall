#include "macos.h"

static void split_arrange(Mel_Layout* layout, Mel_Gui_Handle container)
{
    (void)layout;
    Mel_Gui_Node* node = mel_gui__node(container);
    if (!node || !node->native) return;

    u32 count = 0;
    Mel_Gui_Node* data = mel_gui__nodes(&count);
    for (u32 i = 0; i < count; i++) {
        Mel_Gui_Node* c = &data[i];
        if (!mel_gui_handle_eq(c->parent, container)) continue;
        if (!c->native) continue;
        NSView* host = (__bridge NSView*)c->native;
        NSRect  fr   = host.frame;
        c->x = 0;
        c->y = 0;
        c->width  = (i32)fr.size.width;
        c->height = (i32)fr.size.height;
        if (c->layout) mel_gui__layout_arrange(c->self);
    }
}

static const Mel_Layout_Vtable s_split_vtable = { .arrange = split_arrange };

@implementation MelGuiSplitView

- (CGFloat)splitView:(NSSplitView*)splitView
constrainMinCoordinate:(CGFloat)proposedMin
         ofSubviewAt:(NSInteger)dividerIndex
{
    if (dividerIndex < (NSInteger)self.mins.count) {
        return proposedMin + self.mins[dividerIndex].doubleValue;
    }
    return proposedMin;
}

- (CGFloat)splitView:(NSSplitView*)splitView
constrainMaxCoordinate:(CGFloat)proposedMax
         ofSubviewAt:(NSInteger)dividerIndex
{
    NSInteger next = dividerIndex + 1;
    if (next < (NSInteger)self.mins.count) {
        return proposedMax - self.mins[next].doubleValue;
    }
    return proposedMax;
}

@end

Mel_Gui_Handle mel_splitter_create_opt(Mel_Gui_Handle parent, Mel_Splitter_Opt o)
{
    Mel_Layout* layout = (Mel_Layout*)mel_calloc(mel_gui__alloc(), sizeof *layout);
    if (layout) layout->vtable = &s_split_vtable;

    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    @autoreleasepool {
        MelGuiSplitView* sv = [[MelGuiSplitView alloc] initWithFrame:NSMakeRect(0, 0, n->width, n->height)];
        sv.handle       = h;
        sv.focus        = o.focus;
        sv.mins         = [NSMutableArray array];
        sv.vertical     = (o.orientation == MEL_SPLIT_HORIZONTAL);
        sv.dividerStyle = NSSplitViewDividerStyleThin;
        sv.delegate     = sv;
        mel_gui__macos_install_child(n, sv);
    }
    return h;
}

Mel_Gui_Handle mel_splitpane_create_opt(Mel_Gui_Handle splitter, Mel_SplitPane_Opt o)
{
    Mel_Gui_Node* sn = mel_gui__node(splitter);
    if (!sn || !sn->native) return MEL_GUI_HANDLE_NONE;

    Mel_Gui_Handle h = mel_gui__node_new(splitter, 0, 0, 0, 0, o.id, o.user, false,
                                         &o.layoutable, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    @autoreleasepool {
        MelGuiSplitView*     sv   = (__bridge MelGuiSplitView*)sn->native;
        MelGuiContainerView* host = [[MelGuiContainerView alloc] initWithFrame:sv.bounds];
        host.handle = h;
        [sv addSubview:host];
        [sv.mins addObject:@(o.min_size > 0 ? o.min_size : 0)];
        [sv adjustSubviews];

        n->native  = (void*)CFBridgingRetain(host);
        n->content = (__bridge void*)host;
    }
    return h;
}
