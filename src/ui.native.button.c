#include "ui.native.button.h"

extern const Mel_NCtrl_VTable* mel__nbutton_vtable_osx(void);
extern void mel__nbutton_set_text_platform(Mel_NButton* button, const char* text);

void mel_nbutton_init_opt(Mel_NButton* button, Mel_NButton_Opt opt)
{
    assert(button != nullptr);

    mel_nctrl_init(&button->base, mel__nbutton_vtable_osx());

    button->text      = opt.text ? opt.text : "Button";
    button->on_click  = nullptr;
    button->user_data = nullptr;
}

void mel_nbutton_set_text(Mel_NButton* button, const char* text)
{
    assert(button != nullptr);
    button->text = text;
    if (button->base.backing)
        mel__nbutton_set_text_platform(button, text);
}
