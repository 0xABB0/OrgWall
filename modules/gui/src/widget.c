#include "gui_internal.h"

/* Portable handle accessors. The native ops (set_text/get_text/set_bounds/
 * set_visible/set_enabled/set_focus/invalidate) are defined directly in
 * src/<backend>/, not here — the backend is the implementation. */

u32 mel_gui_id(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    return n ? n->id : 0;
}

void mel_gui_set_user(Mel_Gui_Handle h, void* user)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (n) n->user = user;
}

void* mel_gui_user(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    return n ? n->user : NULL;
}

void* mel_gui_native_handle(Mel_Gui_Handle h)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    return n ? n->native : NULL;
}
