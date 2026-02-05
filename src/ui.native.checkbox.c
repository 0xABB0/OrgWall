#include "ui.native.checkbox.h"

extern const Mel_NCtrl_VTable* mel__ncheckbox_vtable_osx(void);
extern void mel__ncheckbox_set_text_platform(Mel_NCheckbox* checkbox, const char* text);
extern void mel__ncheckbox_set_checked_platform(Mel_NCheckbox* checkbox, bool checked);

void mel_ncheckbox_init_opt(Mel_NCheckbox* checkbox, Mel_NCheckbox_Opt opt)
{
    assert(checkbox != nullptr);

    mel_nctrl_init(&checkbox->base, mel__ncheckbox_vtable_osx());

    checkbox->text      = opt.text ? opt.text : "";
    checkbox->checked   = opt.checked;
    checkbox->on_change = nullptr;
    checkbox->user_data = nullptr;
}

void mel_ncheckbox_set_text(Mel_NCheckbox* checkbox, const char* text)
{
    assert(checkbox != nullptr);
    checkbox->text = text;
    if (checkbox->base.backing)
        mel__ncheckbox_set_text_platform(checkbox, text);
}

void mel_ncheckbox_set_checked(Mel_NCheckbox* checkbox, bool checked)
{
    assert(checkbox != nullptr);
    checkbox->checked = checked;
    if (checkbox->base.backing)
        mel__ncheckbox_set_checked_platform(checkbox, checked);
}
