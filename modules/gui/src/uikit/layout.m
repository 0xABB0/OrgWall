#include "uikit.h"

#include <math.h>

/* UIKit scenes are arranged by the portable solver; lowering column/row to
 * UIStackView is tracked in todo.org. */
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
    if (![obj isKindOfClass:[UIView class]])
        return false;

    UIView* v = (UIView*)obj;
    CGSize  s = [v sizeThatFits:CGSizeZero];
    if (s.width <= 0 && s.height <= 0)
        return false;

    if (out_w)
        *out_w = (i32)ceil(s.width);
    if (out_h)
        *out_h = (i32)ceil(s.height);
    return true;
}
