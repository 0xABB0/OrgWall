#include "ui.widget.panel.h"
#include "sprite.batch.h"

static void wpanel_draw(Mel_Widget* w, void* ctx)
{
    Mel_WPanel* panel = (Mel_WPanel*)w;
    Mel_SpriteBatch* batch = (Mel_SpriteBatch*)ctx;
    mel_sprite_batch_draw(batch, w->pos.x, w->pos.y, w->size.x, w->size.y, panel->color);
}

void mel_wpanel_init(Mel_WPanel* panel)
{
    assert(panel != nullptr);
    mel_widget_init(&panel->base);
    panel->base.draw = wpanel_draw;
    panel->color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
}
