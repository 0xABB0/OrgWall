#include "ui.widget.label.h"
#include "font.atlas.h"
#include "string.str8.h"

static void wlabel_draw(Mel_Widget* w, void* ctx)
{
    Mel_WLabel* label = (Mel_WLabel*)w;
    Mel_SpriteBatch* batch = (Mel_SpriteBatch*)ctx;

    if (label->font && label->text.len > 0)
        mel_font_atlas_draw_text(label->font, batch, label->text, w->pos.x, w->pos.y, label->text_color);
}

void mel_wlabel_init(Mel_WLabel* label)
{
    assert(label != nullptr);
    mel_widget_init(&label->base);
    label->base.draw = wlabel_draw;
    label->font = nullptr;
    label->text = (str8){0};
    label->text_color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
}

void mel_wlabel_set_text(Mel_WLabel* label, str8 text)
{
    assert(label != nullptr);
    label->text = text;
}
