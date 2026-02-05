#include "ui.native.menuitem.h"
#include "ui.native.menu.h"

extern const Mel_NCtrl_VTable* mel__nmenuitem_vtable_osx(void);
extern void mel__nmenuitem_set_title_platform(Mel_NMenuItem* item, const char* title);
extern void mel__nmenuitem_set_submenu_platform(Mel_NMenuItem* item, Mel_NMenu* submenu);

void mel_nmenuitem_init_opt(Mel_NMenuItem* item, Mel_NMenuItem_Opt opt)
{
    assert(item != nullptr);

    mel_nctrl_init(&item->base, mel__nmenuitem_vtable_osx());

    item->title          = opt.title ? opt.title : "";
    item->key_equivalent = opt.key_equivalent ? opt.key_equivalent : "";
    item->submenu        = nullptr;
    item->on_action      = nullptr;
    item->user_data      = nullptr;
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
