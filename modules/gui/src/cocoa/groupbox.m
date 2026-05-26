#include "macos.h"

Mel_Gui_Handle mel_groupbox_create_opt(Mel_Gui_Handle parent, Mel_GroupBox_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    @autoreleasepool {
        NSBox* box = [[NSBox alloc] initWithFrame:NSMakeRect(0, 0, n->width, n->height)];
        box.boxType       = NSBoxPrimary;
        box.titlePosition = NSAtTop;
        box.title         = mel_gui__macos_nsstring(o.title);

        MelGuiContainerView* host = [[MelGuiContainerView alloc] initWithFrame:box.bounds];
        host.handle = h;
        host.focus  = o.focus;
        box.contentView = host;

        mel_gui__macos_install_child(n, box);
        n->content = (__bridge void*)host;
    }
    return h;
}
