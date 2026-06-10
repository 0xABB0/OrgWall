#include "linux.h"

/* XCB core has no layout engine; the portable solver arranging absolute
 * windows is the only path. */
bool mel_gui__backend_layout_adopt(Mel_Gui_Node* n, Mel_Layout* layout)
{
    (void)n;
    (void)layout;
    return false;
}

bool mel_gui__backend_natural_size(Mel_Gui_Node* n, i32* out_w, i32* out_h)
{
    (void)n;
    (void)out_w;
    (void)out_h;
    return false;
}
