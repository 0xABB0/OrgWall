#include "ui.native.edit.h"

extern const Mel_NCtrl_VTable* mel__nedit_vtable_osx(void);
extern void mel__nedit_set_text_platform(Mel_NEdit* edit, const char* text);
extern void mel__nedit_set_placeholder_platform(Mel_NEdit* edit, const char* placeholder);
extern const char* mel__nedit_get_text_platform(Mel_NEdit* edit);

void mel_nedit_init_opt(Mel_NEdit* edit, Mel_NEdit_Opt opt)
{
    assert(edit != nullptr);

    mel_nctrl_init(&edit->base, mel__nedit_vtable_osx());

    edit->text        = opt.text ? opt.text : "";
    edit->placeholder = opt.placeholder ? opt.placeholder : "";
    edit->on_change   = nullptr;
    edit->on_confirm  = nullptr;
    edit->user_data   = nullptr;
}

void mel_nedit_set_text(Mel_NEdit* edit, const char* text)
{
    assert(edit != nullptr);
    edit->text = text;
    if (edit->base.backing)
        mel__nedit_set_text_platform(edit, text);
}

void mel_nedit_set_placeholder(Mel_NEdit* edit, const char* placeholder)
{
    assert(edit != nullptr);
    edit->placeholder = placeholder;
    if (edit->base.backing)
        mel__nedit_set_placeholder_platform(edit, placeholder);
}

const char* mel_nedit_get_text(Mel_NEdit* edit)
{
    assert(edit != nullptr);
    if (edit->base.backing)
        return mel__nedit_get_text_platform(edit);
    return edit->text;
}
