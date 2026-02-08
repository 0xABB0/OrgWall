#include "ui.native.listbox.h"

void mel_nlistbox_init_opt(Mel_NListbox* listbox, Mel_NListbox_Opt opt)
{
    assert(listbox != nullptr);

    mel_nctrl_init(&listbox->base, mel__nlistbox_vtable());

    listbox->items      = opt.items;
    listbox->item_count = opt.item_count;
    listbox->selected   = -1;
    listbox->on_select  = nullptr;
    listbox->user_data  = nullptr;

    mel_nctrl_create_backing(&listbox->base);

    if (listbox->base.backing && listbox->items && listbox->item_count > 0)
        mel__nlistbox_set_items_platform(listbox, listbox->items, listbox->item_count);
}

void mel_nlistbox_set_items(Mel_NListbox* listbox, const char** items, i32 count)
{
    assert(listbox != nullptr);
    listbox->items = items;
    listbox->item_count = count;
    if (listbox->base.backing)
        mel__nlistbox_set_items_platform(listbox, items, count);
}

void mel_nlistbox_set_selected(Mel_NListbox* listbox, i32 index)
{
    assert(listbox != nullptr);
    listbox->selected = index;
    if (listbox->base.backing)
        mel__nlistbox_set_selected_platform(listbox, index);
}
