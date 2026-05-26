#include "uikit.h"

@implementation MelDialogWindow
@end

Mel_Gui_Handle mel_dialog_create_opt(Mel_Dialog_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(MEL_GUI_HANDLE_NONE, o.x, o.y, o.w, o.h, 0, o.user,
                                         false, NULL, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    mel_gui__ios_sync(^{
        MelDialogWindow*  window = [[MelDialogWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
        UIViewController* vc     = [[UIViewController alloc] init];
        UIView*           root   = vc.view;
        root.backgroundColor      = [UIColor systemBackgroundColor];
        window.rootViewController  = vc;
        window.handle              = h;
        window.on_                 = o.on_;
        window.windowLevel         = UIWindowLevelAlert;

        n->native  = (void*)CFBridgingRetain(window);
        n->content = (void*)CFBridgingRetain(root);
        [window makeKeyAndVisible];
    });

    n->x = 0;
    n->y = 0;
    return h;
}

void mel_dialog_close(Mel_Gui_Handle dialog, i32 result)
{
    Mel_Gui_Node* n = mel_gui__node(dialog);
    if (!n || !n->native) return;

    id obj = (__bridge id)n->native;
    if ([obj isKindOfClass:[MelDialogWindow class]]) {
        MelDialogWindow* w = (MelDialogWindow*)obj;
        if (w.on_.on_result) w.on_.on_result(dialog, result, mel_gui_user(dialog));
    }
    mel_gui_destroy(dialog);
}
