#include "macos.h"

@implementation MelGuiLabel
@end

Mel_Gui_Handle mel_label_create_opt(Mel_Gui_Handle parent, Mel_Label_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden,
                                         &o.layoutable, NULL);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    @autoreleasepool {
        MelGuiLabel* label = [[MelGuiLabel alloc] initWithFrame:NSMakeRect(0, 0, n->width, n->height)];
        label.handle          = h;
        label.editable        = NO;
        label.selectable      = NO;
        label.bezeled         = NO;
        label.drawsBackground = NO;
        label.bordered        = NO;
        label.stringValue     = mel_gui__macos_nsstring(o.text);
        label.alignment       = NSTextAlignmentLeft;
        label.lineBreakMode   = NSLineBreakByWordWrapping;
        label.cell.wraps      = YES;
        label.cell.scrollable = NO;

        mel_gui__macos_install_child(n, label);
    }
    return h;
}
