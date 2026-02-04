#include "ui_panel.h"

static void panel_draw(Mel_Widget* w, Mel_SpriteBatch* batch)
{
    mel_sprite_batch_draw(batch, w->pos.x, w->pos.y, w->size.x, w->size.y, w->color);
}

static const Mel_Widget_VTable s_panel_vtable = {
    .draw = panel_draw,
};

void mel_panel_init(Mel_Panel* panel)
{
    assert(panel != nullptr);
    mel_widget_init(&panel->base, &s_panel_vtable, MEL_WIDGET_TYPE_PANEL);
}
