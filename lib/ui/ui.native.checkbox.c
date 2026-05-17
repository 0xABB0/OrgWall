#include "ui.native.checkbox.h"
#include "str8.h"

void mel_ncheckbox_init_opt(Mel_NCheckbox* checkbox, Mel_NCheckbox_Opt opt)
{
    assert(checkbox != nullptr);

    mel_nctrl_init(&checkbox->base, mel__ncheckbox_vtable());

    checkbox->text      = str8_is_empty(opt.text) ? S8("") : opt.text;
    checkbox->checked   = opt.checked;
    checkbox->on_change = nullptr;
    checkbox->user_data = nullptr;

    mel_nctrl_create_backing(&checkbox->base);
}

void mel_ncheckbox_set_text(Mel_NCheckbox* checkbox, str8 text)
{
    assert(checkbox != nullptr);
    checkbox->text = text;
    if (checkbox->base.backing) {
        char text_buf[1024];
        str8_to_buf(text, text_buf, sizeof(text_buf));
        mel__ncheckbox_set_text_platform(checkbox, text_buf);
    }
}

void mel_ncheckbox_set_checked(Mel_NCheckbox* checkbox, bool checked)
{
    assert(checkbox != nullptr);
    checkbox->checked = checked;
    if (checkbox->base.backing)
        mel__ncheckbox_set_checked_platform(checkbox, checked);
}
