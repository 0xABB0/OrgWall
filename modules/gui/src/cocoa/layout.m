#include "macos.h"

#include <math.h>

/* AppKit windows are arranged by the portable solver; lowering column/row to
 * NSStackView is tracked in todo.org. */
bool mel_gui__backend_layout_adopt(Mel_Gui_Node* n, Mel_Layout* layout)
{
    (void)n;
    (void)layout;
    return false;
}

bool mel_gui__backend_natural_size(Mel_Gui_Node* n, i32* out_w, i32* out_h)
{
    if (!n || !n->native)
        return false;

    id obj = (__bridge id)n->native;
    if (![obj isKindOfClass:[NSView class]])
        return false;

    NSView* v = (NSView*)obj;
    NSSize  s = v.fittingSize;
    if (s.width <= 0 && s.height <= 0)
        return false;

    if (out_w)
        *out_w = (i32)ceil(s.width);
    if (out_h)
        *out_h = (i32)ceil(s.height);
    return true;
}
