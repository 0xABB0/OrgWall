#include "ui.native.button.h"
#include "str8.h"

void mel_nbutton_init_opt(Mel_NButton* button, Mel_NButton_Opt opt)
{
    assert(button != nullptr);

    mel_nctrl_init(&button->base, mel__nbutton_vtable());

    button->text      = str8_is_empty(opt.text) ? S8("Button") : opt.text;
    button->on_click  = nullptr;
    button->user_data = nullptr;

    mel_nctrl_create_backing(&button->base);
}

void mel_nbutton_set_text(Mel_NButton* button, str8 text)
{
    assert(button != nullptr);
    button->text = text;
    if (button->base.backing) {
        char text_buf[1024];
        str8_to_buf(text, text_buf, sizeof(text_buf));
        mel__nbutton_set_text_platform(button, text_buf);
    }
}
