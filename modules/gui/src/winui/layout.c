#include "win32.h"

/* Absolute positioning is the win32 idiom; the portable solver arranging and
 * pushing bounds IS the native path here, not a fallback. */
bool mel_gui__backend_layout_adopt(Mel_Gui_Node* n, Mel_Layout* layout)
{
    (void)n;
    (void)layout;
    return false;
}

/* Native text measurement (GetTextExtentPoint32 per control font) is tracked
 * in todo.org; until then the host falls back to the node's current extent. */
bool mel_gui__backend_natural_size(Mel_Gui_Node* n, i32* out_w, i32* out_h)
{
    (void)n;
    (void)out_w;
    (void)out_h;
    return false;
}
