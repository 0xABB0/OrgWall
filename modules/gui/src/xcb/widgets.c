#include "linux.h"

#include <gui/controls/label.h>
#include <gui/controls/button.h>

#include <log/log.h>

/* XCB core protocol has no text-rendering or push-button widget — those live in a
 * toolkit (Xft/Pango/GTK), deliberately out of this minimal-viable backend. A
 * label and a button are therefore child windows that occupy their layout slot
 * (so geometry, parenting and the gpu_view's placement are correct) but render no
 * glyphs and fire no native click. This is stubbed LOUDLY (MEL-ENGINE-VIII): the
 * surface is honest, not silently degraded. */

static bool g_warned_text;

static void mel_gui__xcb_warn_no_text(const char* what)
{
    if (g_warned_text)
        return;
    g_warned_text = true;
    mel_log_warn("gui", "xcb backend: %s renders no text (XCB core has no font widget); slot is occupied but blank — install an Xft/Pango text path to draw glyphs", what);
}

Mel_Gui_Handle mel_label_create_opt(Mel_Gui_Handle parent, Mel_Label_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden, &o.layoutable, NULL);
    Mel_Gui_Node*  n = mel_gui__node(h);
    if (!n)
        return h;

    mel_gui__xcb_warn_no_text("label");
    mel_xcb_window w = mel_gui__xcb_create_child(n, 0);
    n->native = (void*)(uintptr_t)w;
    return h;
}

Mel_Gui_Handle mel_button_create_opt(Mel_Gui_Handle parent, Mel_Button_Opt o)
{
    Mel_Gui_Handle h = mel_gui__node_new(parent, o.x, o.y, o.w, o.h, o.id, o.user, o.hidden, &o.layoutable, NULL);
    Mel_Gui_Node*  n = mel_gui__node(h);
    if (!n)
        return h;

    mel_gui__xcb_warn_no_text("button");
    mel_xcb_window w = mel_gui__xcb_create_child(n, MEL_XCB_EVENT_MASK_BUTTON_PRESS | MEL_XCB_EVENT_MASK_BUTTON_RELEASE);
    n->native = (void*)(uintptr_t)w;
    return h;
}
