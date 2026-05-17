#include "ui.native.combo.h"
#include "str8.h"

void mel_ncombo_init_opt(Mel_NCombo* combo, Mel_NCombo_Opt opt)
{
    assert(combo != nullptr);

    mel_nctrl_init(&combo->base, mel__ncombo_vtable());

    combo->items      = opt.items;
    combo->item_count = opt.item_count;
    combo->selected   = opt.selected;
    combo->on_select  = nullptr;
    combo->user_data  = nullptr;

    mel_nctrl_create_backing(&combo->base);

    if (combo->base.backing && combo->items && combo->item_count > 0)
        mel__ncombo_set_items_platform(combo, nullptr, combo->item_count);

    if (combo->base.backing && combo->selected >= 0)
        mel__ncombo_set_selected_platform(combo, combo->selected);
}

void mel_ncombo_set_items(Mel_NCombo* combo, str8* items, i32 count)
{
    assert(combo != nullptr);
    combo->items = items;
    combo->item_count = count;
    if (combo->base.backing)
        mel__ncombo_set_items_platform(combo, nullptr, count);
}

void mel_ncombo_set_selected(Mel_NCombo* combo, i32 index)
{
    assert(combo != nullptr);
    combo->selected = index;
    if (combo->base.backing)
        mel__ncombo_set_selected_platform(combo, index);
}
