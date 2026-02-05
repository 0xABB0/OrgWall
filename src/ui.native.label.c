#include "ui.native.label.h"

extern const Mel_NCtrl_VTable* mel__nlabel_vtable_osx(void);
extern void mel__nlabel_set_text_platform(Mel_NLabel* label, const char* text);
extern void mel__nlabel_set_font_size_platform(Mel_NLabel* label, f32 size);

void mel_nlabel_init_opt(Mel_NLabel* label, Mel_NLabel_Opt opt)
{
    assert(label != nullptr);

    mel_nctrl_init(&label->base, mel__nlabel_vtable_osx());

    label->text      = opt.text ? opt.text : "";
    label->font_size = opt.font_size > 0 ? opt.font_size : 13.0f;
}

void mel_nlabel_set_text(Mel_NLabel* label, const char* text)
{
    assert(label != nullptr);
    label->text = text;
    if (label->base.backing)
        mel__nlabel_set_text_platform(label, text);
}

void mel_nlabel_set_font_size(Mel_NLabel* label, f32 size)
{
    assert(label != nullptr);
    label->font_size = size;
    if (label->base.backing)
        mel__nlabel_set_font_size_platform(label, size);
}
