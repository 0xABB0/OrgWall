#include "ui.native.button.h"

void mel_nbutton_init_opt(Mel_NButton* button, Mel_NButton_Opt opt)
{
    assert(button != nullptr);

    mel_nctrl_init(&button->base, mel__nbutton_vtable());

    button->text      = opt.text ? opt.text : "Button";
    button->on_click  = nullptr;
    button->user_data = nullptr;

    mel_nctrl_create_backing(&button->base);
}

void mel_nbutton_set_text(Mel_NButton* button, const char* text)
{
    assert(button != nullptr);
    button->text = text;
    if (button->base.backing)
        mel__nbutton_set_text_platform(button, text);
}
