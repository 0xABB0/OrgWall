#include "ui.native.radio.h"
#include "string.str8.h"

void mel_nradio_init_opt(Mel_NRadio* radio, Mel_NRadio_Opt opt)
{
    assert(radio != nullptr);

    mel_nctrl_init(&radio->base, mel__nradio_vtable());

    radio->text      = str8_is_empty(opt.text) ? S8("") : opt.text;
    radio->group_id  = opt.group_id;
    radio->selected  = opt.selected;
    radio->on_change = nullptr;
    radio->user_data = nullptr;

    mel_nctrl_create_backing(&radio->base);
}

void mel_nradio_set_text(Mel_NRadio* radio, str8 text)
{
    assert(radio != nullptr);
    radio->text = text;
    if (radio->base.backing) {
        char text_buf[1024];
        str8_to_buf(text, text_buf, sizeof(text_buf));
        mel__nradio_set_text_platform(radio, text_buf);
    }
}

void mel_nradio_set_selected(Mel_NRadio* radio, bool selected)
{
    assert(radio != nullptr);
    radio->selected = selected;
    if (radio->base.backing)
        mel__nradio_set_selected_platform(radio, selected);
}
