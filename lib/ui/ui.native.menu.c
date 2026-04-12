#include "ui.native.menu.h"
#include "string.str8.h"

void mel_nmenu_init_opt(Mel_NMenu* menu, Mel_NMenu_Opt opt)
{
    assert(menu != nullptr);

    mel_nctrl_init(&menu->base, mel__nmenu_vtable());

    menu->title = str8_is_empty(opt.title) ? S8("") : opt.title;

    mel_nctrl_create_backing(&menu->base);
}

void mel_nmenu_set_title(Mel_NMenu* menu, str8 title)
{
    assert(menu != nullptr);
    menu->title = title;
    if (menu->base.backing) {
        char title_buf[256];
        str8_to_buf(title, title_buf, sizeof(title_buf));
        mel__nmenu_set_title_platform(menu, title_buf);
    }
}

void mel_nmenu_popup(Mel_NMenu* menu, Mel_Vec2 location, Mel_NCtrl* in_view)
{
    assert(menu != nullptr);
    assert(in_view != nullptr);
    mel__nmenu_popup_platform(menu, location, in_view);
}
