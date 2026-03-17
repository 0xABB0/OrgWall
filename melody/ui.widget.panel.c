#include "ui.widget.panel.h"

static void wpanel_draw(Mel_Widget* w, void* ctx)
{
    (void)w;
    (void)ctx;
}

void mel_wpanel_init(Mel_WPanel* panel)
{
    assert(panel != nullptr);
    mel_widget_init(&panel->base);
    panel->base.draw = wpanel_draw;
    panel->color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
}
