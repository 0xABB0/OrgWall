#include "ui.native.menuitem.h"
#include "ui.native.menu.h"
#include "string.str8.h"

void mel_nmenuitem_init_opt(Mel_NMenuItem* item, Mel_NMenuItem_Opt opt)
{
    assert(item != nullptr);

    mel_nctrl_init(&item->base, mel__nmenuitem_vtable());

    item->title          = str8_is_empty(opt.title) ? S8("") : opt.title;
    item->key_equivalent = str8_is_empty(opt.key_equivalent) ? S8("") : opt.key_equivalent;
    item->submenu        = nullptr;
    item->on_action      = nullptr;
    item->user_data      = nullptr;

    mel_nctrl_create_backing(&item->base);
}

void mel_nmenuitem_set_title(Mel_NMenuItem* item, str8 title)
{
    assert(item != nullptr);
    item->title = title;
    if (item->base.backing) {
        char title_buf[256];
        str8_to_buf(title, title_buf, sizeof(title_buf));
        mel__nmenuitem_set_title_platform(item, title_buf);
    }
}

void mel_nmenuitem_set_submenu(Mel_NMenuItem* item, Mel_NMenu* submenu)
{
    assert(item != nullptr);
    item->submenu = submenu;
    if (item->base.backing)
        mel__nmenuitem_set_submenu_platform(item, submenu);
}
