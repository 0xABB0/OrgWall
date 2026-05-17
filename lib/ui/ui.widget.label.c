#include "ui.widget.label.h"
#include "font.atlas.h"
#include "collection.slotmap.h"
#include "str8.h"

static void wlabel_draw(Mel_Widget* w, void* ctx)
{
    (void)w;
    (void)ctx;
}

void mel_wlabel_init(Mel_WLabel* label)
{
    assert(label != nullptr);
    mel_widget_init(&label->base);
    label->base.draw = wlabel_draw;
    label->font = MEL_FONT_ATLAS_HANDLE_NULL;
    label->text = (str8){0};
    label->text_color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
}

void mel_wlabel_set_text(Mel_WLabel* label, str8 text)
{
    assert(label != nullptr);
    label->text = text;
}
