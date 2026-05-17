#include "ui.native.label.h"
#include "str8.h"

void mel_nlabel_init_opt(Mel_NLabel* label, Mel_NLabel_Opt opt)
{
    assert(label != nullptr);

    mel_nctrl_init(&label->base, mel__nlabel_vtable());

    label->text      = str8_is_empty(opt.text) ? S8("") : opt.text;
    label->font_size = opt.font_size > 0 ? opt.font_size : 13.0f;

    mel_nctrl_create_backing(&label->base);
}

void mel_nlabel_set_text(Mel_NLabel* label, str8 text)
{
    assert(label != nullptr);
    label->text = text;
    if (label->base.backing) {
        char text_buf[1024];
        str8_to_buf(text, text_buf, sizeof(text_buf));
        mel__nlabel_set_text_platform(label, text_buf);
    }
}

void mel_nlabel_set_font_size(Mel_NLabel* label, f32 size)
{
    assert(label != nullptr);
    label->font_size = size;
    if (label->base.backing)
        mel__nlabel_set_font_size_platform(label, size);
}
