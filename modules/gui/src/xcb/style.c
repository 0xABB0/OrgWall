#include "linux.h"

#include <log/log.h>

static bool g_warned_style;

static void warn_style_once(void)
{
    if (g_warned_style)
        return;
    g_warned_style = true;
    mel_log_warn("gui", "xcb backend: only background color is stylable (XCB core draws no text, borders or rounded corners); other style fields are ignored");
}

void mel_gui_set_style(Mel_Gui_Handle h, Mel_Style style)
{
    Mel_Gui_Node* n = mel_gui__node(h);
    if (!n || !n->native)
        return;

    Mel_Xcb_State* x = mel_gui__xcb();
    if (!x)
        return;

    if (style.fg.set || style.border_color.set || style.border_width || style.corner_radius || style.font_family.len || style.font_size || style.font_weight || style.italic)
        warn_style_once();

    if (!style.bg.set)
        return;

    /* Pixel composition assumes a TrueColor visual, like the rest of this
     * backend (the root window's depth-24 visual). */
    u32 pixel = ((u32)style.bg.color.r << 16) | ((u32)style.bg.color.g << 8) | (u32)style.bg.color.b;
    u32 values[] = { pixel };
    x->api.change_window_attributes(x->conn, (mel_xcb_window)(uintptr_t)n->native, MEL_XCB_CW_BACK_PIXEL, values);
    x->api.flush(x->conn);
}
