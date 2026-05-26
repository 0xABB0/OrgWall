#include "uikit.h"

@implementation MelDialogController
@end

Mel_Gui_Handle mel_dialog_create_opt(Mel_Dialog_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(MEL_GUI_HANDLE_NONE, o.x, o.y, o.w, o.h, 0, o.user,
                                         false, NULL, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;
    NSString* title = mel_gui__ios_nsstring(o.title);

    MelDialogController* vc = [[MelDialogController alloc] init];
    vc.frame_handle = h;
    vc.inset_mode   = MEL_FRAME_PAD;
    vc.dlg_on       = o.on_;
    vc.title        = title;
    (void)vc.view;

    n->native  = (void*)CFBridgingRetain(vc);
    n->content = (void*)CFBridgingRetain(vc.content);

    UINavigationController* nav = mel_gui__ios_nav();
    UIViewController* presenter = nav.topViewController ?: (UIViewController*)nav;
    [presenter presentViewController:vc animated:YES completion:nil];

    n->x = 0;
    n->y = 0;
    return h;
}

void mel_dialog_close(Mel_Gui_Handle dialog, i32 result)
{
    Mel_Gui_Node* n = mel_gui__node(dialog);
    if (!n || !n->native) return;

    id obj = (__bridge id)n->native;
    if ([obj isKindOfClass:[MelDialogController class]]) {
        MelDialogController* vc = (MelDialogController*)obj;
        if (vc.dlg_on.on_result) vc.dlg_on.on_result(dialog, result, mel_gui_user(dialog));
    }
    mel_gui_destroy(dialog);
}
