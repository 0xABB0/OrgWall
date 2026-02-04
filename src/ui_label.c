#include "ui_label.h"
#include "font.h"

#define MEL_WIDGET_TYPE_LABEL 2

static void label_draw(Mel_Widget* w, Mel_SpriteBatch* batch)
{
    Mel_Label* label = (Mel_Label*)w;

    if (label->font && label->text && label->text[0])
    {
        mel_font_draw_text(label->font, batch, label->text, w->pos.x, w->pos.y, label->text_color);
    }
}

static const Mel_Widget_VTable s_label_vtable = {
    .draw = label_draw,
};

void mel_label_init(Mel_Label* label)
{
    assert(label != nullptr);
    mel_widget_init(&label->base, &s_label_vtable, MEL_WIDGET_TYPE_LABEL);

    label->font = nullptr;
    label->text = nullptr;
    label->text_color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
}

void mel_label_set_text(Mel_Label* label, const char* text)
{
    assert(label != nullptr);
    label->text = text;
}
