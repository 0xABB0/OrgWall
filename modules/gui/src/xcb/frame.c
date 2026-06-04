#include "linux.h"

#include <gui/controls/frame.h>
#include <gui/appkit/frame.h>

#include <string.h>

Mel_Gui_Handle mel_frame_create_opt(Mel_Frame_Opt o)
{
    Mel_Xcb_State* x = mel_gui__xcb();

    Mel_Gui_Handle h = mel_gui__node_new(MEL_GUI_HANDLE_NONE, o.x, o.y, o.w, o.h, 0, o.user, o.initial_state == MEL_FRAME_HIDDEN, NULL, o.layout);
    Mel_Gui_Node*  n = mel_gui__node(h);
    if (!n)
        return h;

    if (!x->ok)
        return h;

    i32 cw = n->width > 0 ? n->width : 480;
    i32 ch = n->height > 0 ? n->height : 360;

    mel_xcb_window wid = x->api.generate_id(x->conn);
    u32            mask = MEL_XCB_CW_BACK_PIXEL | MEL_XCB_CW_EVENT_MASK;
    u32            values[2] = { x->black_pixel, MEL_XCB_EVENT_MASK_STRUCTURE_NOTIFY };

    x->api.create_window(x->conn, MEL_XCB_COPY_FROM_PARENT, wid, x->root, (i16)n->x, (i16)n->y, (u16)cw, (u16)ch, 0, MEL_XCB_WINDOW_CLASS_INPUT_OUTPUT, x->visual, mask, values);

    if (o.title.len > 0 && o.title.data)
        x->api.change_property(x->conn, MEL_XCB_PROP_MODE_REPLACE, wid, MEL_XCB_ATOM_WM_NAME, MEL_XCB_ATOM_STRING, 8, (u32)o.title.len, o.title.data);

    if (x->wm_protocols && x->wm_delete_window)
        x->api.change_property(x->conn, MEL_XCB_PROP_MODE_REPLACE, wid, x->wm_protocols, 4, 32, 1, &x->wm_delete_window);

    n->native = (void*)(uintptr_t)wid;
    n->width = cw;
    n->height = ch;

    if (o.initial_state != MEL_FRAME_HIDDEN)
        x->api.map_window(x->conn, wid);
    x->api.flush(x->conn);

    mel_gui__frames_inc();
    return h;
}

Mel_Frame_Insets mel_frame_insets(Mel_Gui_Handle h)
{
    (void)h;
    return (Mel_Frame_Insets){ 0 };
}

Mel_Gui_Handle mel_gui__screen_new(Mel_Gui_Handle window)
{
    Mel_Gui_Handle h = mel_gui__node_new(window, 0, 0, 0, 0, 0, NULL, false, NULL, NULL);
    Mel_Gui_Node*  n = mel_gui__node(h);
    if (!n)
        return h;
    n->is_screen = true;

    Mel_Gui_Node* w = mel_gui__node(window);
    if (w)
    {
        n->width = w->width;
        n->height = w->height;
    }

    mel_xcb_window child = mel_gui__xcb_create_child(n, 0);
    n->native = (void*)(uintptr_t)child;
    return h;
}
