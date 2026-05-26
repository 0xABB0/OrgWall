#include "uikit.h"

Mel_Gui_Handle mel_frame_create_opt(Mel_Frame_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(MEL_GUI_HANDLE_NONE, o.x, o.y, o.w, o.h, 0, o.user,
                                         o.initial_state == MEL_FRAME_HIDDEN, NULL, o.layout);
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n) return h;

    mel_gui__ios_sync(^{
        UIWindow*         window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
        UIViewController* vc     = [[UIViewController alloc] init];
        UIView*           root   = vc.view;
        root.backgroundColor   = [UIColor systemBackgroundColor];
        window.rootViewController = vc;

        n->native  = (void*)CFBridgingRetain(window);
        n->content = (void*)CFBridgingRetain(root);

        if (o.initial_state != MEL_FRAME_HIDDEN) [window makeKeyAndVisible];
        mel_gui__frames_inc();
    });

    // The window owns the screen; children lay out against its content view.
    n->x = 0;
    n->y = 0;
    return h;
}
