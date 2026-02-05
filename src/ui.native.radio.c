#include "ui.native.radio.h"

extern const Mel_NCtrl_VTable* mel__nradio_vtable_osx(void);
extern void mel__nradio_set_text_platform(Mel_NRadio* radio, const char* text);
extern void mel__nradio_set_selected_platform(Mel_NRadio* radio, bool selected);

void mel_nradio_init_opt(Mel_NRadio* radio, Mel_NRadio_Opt opt)
{
    assert(radio != nullptr);

    mel_nctrl_init(&radio->base, mel__nradio_vtable_osx());

    radio->text      = opt.text ? opt.text : "";
    radio->group_id  = opt.group_id;
    radio->selected  = opt.selected;
    radio->on_change = nullptr;
    radio->user_data = nullptr;
}

void mel_nradio_set_text(Mel_NRadio* radio, const char* text)
{
    assert(radio != nullptr);
    radio->text = text;
    if (radio->base.backing)
        mel__nradio_set_text_platform(radio, text);
}

void mel_nradio_set_selected(Mel_NRadio* radio, bool selected)
{
    assert(radio != nullptr);
    radio->selected = selected;
    if (radio->base.backing)
        mel__nradio_set_selected_platform(radio, selected);
}
