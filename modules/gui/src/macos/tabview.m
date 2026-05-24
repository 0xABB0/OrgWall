#include "macos.h"

static void tab_arrange(Mel_Layout* layout, Mel_Gui_Handle container)
{
    (void)layout;
    Mel_Gui_Node* node = mel_gui__node(container);
    if (!node || !node->native) return;

    MelGuiTabView* tv = (__bridge MelGuiTabView*)node->native;
    NSRect rect = tv.contentRect;

    u32 count = 0;
    Mel_Gui_Node* data = mel_gui__nodes(&count);
    for (u32 i = 0; i < count; i++) {
        Mel_Gui_Node* c = &data[i];
        if (!mel_gui_handle_eq(c->parent, container)) continue;
        c->x = 0;
        c->y = 0;
        c->width  = (i32)rect.size.width;
        c->height = (i32)rect.size.height;
        if (c->layout) mel_gui__layout_arrange(c->self);
    }
}

static const Mel_Layout_Vtable s_tab_vtable = { .arrange = tab_arrange };

@implementation MelGuiTabView

- (void)tabView:(NSTabView*)tabView didSelectTabViewItem:(NSTabViewItem*)item
{
    if (!self.on_select) return;
    NSInteger idx = [tabView indexOfTabViewItem:item];
    self.on_select(self.handle, (i32)idx, mel_gui_user(self.handle));
}

@end

Mel_Gui_Handle mel_tabview_create_opt(Mel_Gui_Handle parent, Mel_TabView_Opt o)
{
    Mel_Layout* layout = (Mel_Layout*)mel_calloc(mel_gui__alloc(), sizeof *layout);
    if (layout) layout->vtable = &s_tab_vtable;

    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    @autoreleasepool {
        MelGuiTabView* tv = [[MelGuiTabView alloc] initWithFrame:NSMakeRect(0, 0, n->width, n->height)];
        tv.handle    = h;
        tv.focus     = o.focus;
        tv.on_select = o.on_select;
        tv.delegate  = tv;
        mel_gui__macos_install_child(n, tv);
    }
    return h;
}

Mel_Gui_Handle mel_tab_create_opt(Mel_Gui_Handle tabview, Mel_Tab_Opt o)
{
    Mel_Gui_Node* tn = mel_gui__node(tabview);
    if (!tn || !tn->native) return MEL_GUI_HANDLE_NONE;

    Mel_Gui_Handle h = mel_gui__node_new(tabview, 0, 0, 0, 0, o.id, o.user, false,
                                         &o.layoutable, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    @autoreleasepool {
        MelGuiTabView*       tv   = (__bridge MelGuiTabView*)tn->native;
        MelGuiContainerView* host = [[MelGuiContainerView alloc] initWithFrame:tv.contentRect];
        host.handle = h;

        NSTabViewItem* item = [[NSTabViewItem alloc] initWithIdentifier:nil];
        item.label = mel_gui__macos_nsstring(o.title);
        item.view  = host;
        [tv addTabViewItem:item];

        n->native  = (void*)CFBridgingRetain(host);
        n->content = (__bridge void*)host;
        n->width   = (i32)tv.contentRect.size.width;
        n->height  = (i32)tv.contentRect.size.height;
    }
    return h;
}

void mel_tabview_select(Mel_Gui_Handle tabview, i32 index)
{
    Mel_Gui_Node* n = mel_gui__node(tabview);
    if (!n || !n->native) return;
    MelGuiTabView* tv = (__bridge MelGuiTabView*)n->native;
    if (index >= 0 && index < (i32)tv.numberOfTabViewItems) {
        [tv selectTabViewItemAtIndex:index];
    }
}

i32 mel_tabview_selected(Mel_Gui_Handle tabview)
{
    Mel_Gui_Node* n = mel_gui__node(tabview);
    if (!n || !n->native) return -1;
    MelGuiTabView* tv = (__bridge MelGuiTabView*)n->native;
    NSTabViewItem* sel = tv.selectedTabViewItem;
    return sel ? (i32)[tv indexOfTabViewItem:sel] : -1;
}
