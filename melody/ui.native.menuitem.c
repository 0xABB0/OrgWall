#include "ui.native.menuitem.h"
#include "ui.native.menu.h"

void mel_nmenuitem_init_opt(Mel_NMenuItem* item, Mel_NMenuItem_Opt opt)
{
    assert(item != nullptr);

    mel_nctrl_init(&item->base, mel__nmenuitem_vtable());

    item->title          = opt.title ? opt.title : "";
    item->key_equivalent = opt.key_equivalent ? opt.key_equivalent : "";
    item->submenu        = nullptr;
    item->on_action      = nullptr;
    item->user_data      = nullptr;

    mel_nctrl_create_backing(&item->base);
}

void mel_nmenuitem_set_title(Mel_NMenuItem* item, const char* title)
{
    assert(item != nullptr);
    item->title = title;
    if (item->base.backing)
        mel__nmenuitem_set_title_platform(item, title);
}

void mel_nmenuitem_set_submenu(Mel_NMenuItem* item, Mel_NMenu* submenu)
{
    assert(item != nullptr);
    item->submenu = submenu;
    if (item->base.backing)
        mel__nmenuitem_set_submenu_platform(item, submenu);
}
