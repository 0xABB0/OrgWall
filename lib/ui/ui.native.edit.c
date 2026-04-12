#include "ui.native.edit.h"
#include "string.str8.h"

void mel_nedit_init_opt(Mel_NEdit* edit, Mel_NEdit_Opt opt)
{
    assert(edit != nullptr);

    mel_nctrl_init(&edit->base, mel__nedit_vtable());

    edit->text        = str8_is_empty(opt.text) ? S8("") : opt.text;
    edit->placeholder = str8_is_empty(opt.placeholder) ? S8("") : opt.placeholder;
    edit->on_change   = nullptr;
    edit->on_confirm  = nullptr;
    edit->user_data   = nullptr;

    mel_nctrl_create_backing(&edit->base);
}

void mel_nedit_set_text(Mel_NEdit* edit, str8 text)
{
    assert(edit != nullptr);
    edit->text = text;
    if (edit->base.backing) {
        char buf[1024];
        str8_to_buf(text, buf, sizeof(buf));
        mel__nedit_set_text_platform(edit, buf);
    }
}

void mel_nedit_set_placeholder(Mel_NEdit* edit, str8 placeholder)
{
    assert(edit != nullptr);
    edit->placeholder = placeholder;
    if (edit->base.backing) {
        char buf[256];
        str8_to_buf(placeholder, buf, sizeof(buf));
        mel__nedit_set_placeholder_platform(edit, buf);
    }
}

str8 mel_nedit_get_text(Mel_NEdit* edit)
{
    assert(edit != nullptr);
    if (edit->base.backing)
        return str8_from_cstr(mel__nedit_get_text_platform(edit));
    return edit->text;
}
