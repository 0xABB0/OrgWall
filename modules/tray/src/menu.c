#include <tray/menu.h>
#include <tray/provider.h>

#include <allocator/allocator.h>
#include <collection.slotmap/slotmap.h>
#include <collection.array/array.h>
#include <log/log.h>

#include "tray_internal.h"

static str8 str_dup_local(str8 s)
{
    if (s.len == 0 || s.data == NULL)
        return STR8_EMPTY;
    return str8_dup_alloc(s, mel_tray__alloc());
}

static void str_free_local(str8* s)
{
    if (s->data != NULL)
        mel_dealloc(mel_tray__alloc(), s->data);
    *s = STR8_EMPTY;
}

bool mel_tray_menu_alive(Mel_Tray_Menu m) { return mel_tray__menu_slot(m.h) != NULL; }
bool mel_tray_menu_equal(Mel_Tray_Menu a, Mel_Tray_Menu b) { return a.h.index == b.h.index && a.h.generation == b.h.generation; }

u32 mel_tray_menu_count(Mel_Tray_Menu m)
{
    Menu_Slot* ms = mel_tray__menu_slot(m.h);
    return ms != NULL ? (u32)ms->items.count : 0;
}

static Mel_Tray_Item_Flags normalize_flags(Mel_Tray_Item_Flags f)
{
    Mel_Tray_Item_Flags kind = f & MEL_TRAY_ITEM_KIND_MASK;
    if (kind == 0)
        f |= MEL_TRAY_ITEM_BUTTON;
    return f;
}

static Mel_Tray_Item_Result item_make(Mel_Tray_Menu m, u32 at, Mel_Tray_Item_Desc desc, Mel_SlotMap_Handle submenu)
{
    Mel_Tray_Item_Result r = { .value = MEL_TRAY_ITEM_NULL, .status = MEL_TRAY_OK };
    Menu_Slot*           ms = mel_tray__menu_slot(m.h);
    if (ms == NULL)
    {
        mel_log_error("tray", "item_add on dead menu {index=%u, gen=%u}", m.h.index, m.h.generation);
        r.status = MEL_TRAY_ERROR | MEL_TRAY_ERR_DEAD_HANDLE;
        return r;
    }
    if (at > (u32)ms->items.count)
    {
        mel_log_error("tray", "item insert index %u out of range (count %u)", at, (u32)ms->items.count);
        r.status = MEL_TRAY_ERROR | MEL_TRAY_ERR_INVALID_ARG;
        return r;
    }

    Item_Slot is = { 0 };
    is.tray = ms->tray;
    is.menu = m.h;
    is.submenu = submenu;
    is.label = str_dup_local(desc.label);
    is.flags = normalize_flags(desc.flags);
    is.on_activate = desc.on_activate;
    is.user = desc.user;
    Mel_SlotMap_Handle ih = mel_slotmap_insert(mel_tray__items(), &is);

    mel_array_insert(&ms->items, at, ih);

    Mel_Tray_Provider_Desc* prov = mel_tray__active_provider_desc();
    if (prov != NULL && prov->item_add != NULL)
    {
        Mel_Tray_Item_Lowered lowered = {
            .token = mel_slotmap_handle_pack64(ih),
            .parent_menu_token = mel_slotmap_handle_pack64(m.h),
            .at = at,
            .label = is.label,
            .flags = is.flags,
            .submenu_token = mel_slotmap_handle_valid(submenu) ? mel_slotmap_handle_pack64(submenu) : 0,
        };
        Mel_Tray_Status s = prov->item_add(prov->user, &lowered);
        if (s != MEL_TRAY_OK)
            r.status = s;
    }

    r.value = (Mel_Tray_Item){ ih };
    return r;
}

Mel_Tray_Item_Result mel_tray_item_add(Mel_Tray_Menu m, Mel_Tray_Item_Desc desc)
{
    Menu_Slot* ms = mel_tray__menu_slot(m.h);
    u32        at = ms != NULL ? (u32)ms->items.count : 0;
    return item_make(m, at, desc, MEL_SLOTMAP_HANDLE_NULL);
}

Mel_Tray_Item_Result mel_tray_item_insert(Mel_Tray_Menu m, u32 at, Mel_Tray_Item_Desc desc) { return item_make(m, at, desc, MEL_SLOTMAP_HANDLE_NULL); }

Mel_Tray_Item_Result mel_tray_separator_add(Mel_Tray_Menu m)
{
    Mel_Tray_Item_Desc desc = { .label = STR8_EMPTY, .flags = MEL_TRAY_ITEM_SEPARATOR };
    return mel_tray_item_add(m, desc);
}

static Mel_Tray_Submenu_Result submenu_make(Mel_Tray_Menu m, u32 at, str8 label)
{
    Mel_Tray_Submenu_Result r = { .value = MEL_TRAY_MENU_NULL, .status = MEL_TRAY_OK };
    Menu_Slot*              parent = mel_tray__menu_slot(m.h);
    if (parent == NULL)
    {
        mel_log_error("tray", "submenu_add on dead menu");
        r.status = MEL_TRAY_ERROR | MEL_TRAY_ERR_DEAD_HANDLE;
        return r;
    }

    Menu_Slot sub = { 0 };
    sub.tray = parent->tray;
    mel_array_init(&sub.items, mel_tray__alloc());
    Mel_SlotMap_Handle sub_h = mel_slotmap_insert(mel_tray__menus(), &sub);

    Mel_Tray_Provider_Desc* prov = mel_tray__active_provider_desc();
    if (prov != NULL && prov->menu_create != NULL)
        prov->menu_create(prov->user, mel_slotmap_handle_pack64(sub_h));

    Mel_Tray_Item_Desc   desc = { .label = label, .flags = MEL_TRAY_ITEM_SUBMENU | MEL_TRAY_ITEM_ENABLED };
    Mel_Tray_Item_Result ir = item_make(m, at, desc, sub_h);
    if (!mel_slotmap_handle_valid(ir.value.h))
    {
        if (prov != NULL && prov->menu_destroy != NULL)
            prov->menu_destroy(prov->user, mel_slotmap_handle_pack64(sub_h));
        mel_array_free(&sub.items);
        mel_slotmap_remove(mel_tray__menus(), sub_h);
        r.status = ir.status;
        return r;
    }
    r.value = (Mel_Tray_Menu){ sub_h };
    r.status = ir.status;
    return r;
}

Mel_Tray_Submenu_Result mel_tray_submenu_add(Mel_Tray_Menu m, str8 label)
{
    Menu_Slot* ms = mel_tray__menu_slot(m.h);
    u32        at = ms != NULL ? (u32)ms->items.count : 0;
    return submenu_make(m, at, label);
}

Mel_Tray_Submenu_Result mel_tray_submenu_insert(Mel_Tray_Menu m, u32 at, str8 label) { return submenu_make(m, at, label); }

void        mel_tray__menu_free_recursive(Mel_SlotMap_Handle mh);
static void item_free_recursive(Mel_SlotMap_Handle ih)
{
    Item_Slot* is = mel_tray__item_slot(ih);
    if (is == NULL)
        return;
    Mel_Tray_Provider_Desc* prov = mel_tray__active_provider_desc();
    if (prov != NULL && prov->item_remove != NULL)
        prov->item_remove(prov->user, mel_slotmap_handle_pack64(ih));
    if (mel_slotmap_handle_valid(is->submenu))
        mel_tray__menu_free_recursive(is->submenu);
    str_free_local(&is->label);
    mel_slotmap_remove(mel_tray__items(), ih);
}

void mel_tray__menu_free_recursive(Mel_SlotMap_Handle mh)
{
    Menu_Slot* ms = mel_tray__menu_slot(mh);
    if (ms == NULL)
        return;
    Mel_Tray_Provider_Desc* prov = mel_tray__active_provider_desc();
    for (usize i = 0; i < ms->items.count; i++)
        item_free_recursive(ms->items.items[i]);
    if (prov != NULL && prov->menu_destroy != NULL)
        prov->menu_destroy(prov->user, mel_slotmap_handle_pack64(mh));
    mel_array_free(&ms->items);
    mel_slotmap_remove(mel_tray__menus(), mh);
}

Mel_Tray_Status mel_tray_item_remove(Mel_Tray_Item item)
{
    Item_Slot* is = mel_tray__item_slot(item.h);
    if (is == NULL)
    {
        mel_log_error("tray", "item_remove on dead item");
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_DEAD_HANDLE;
    }
    Menu_Slot* ms = mel_tray__menu_slot(is->menu);
    if (ms != NULL)
    {
        for (usize i = 0; i < ms->items.count; i++)
        {
            if (ms->items.items[i].index == item.h.index && ms->items.items[i].generation == item.h.generation)
            {
                mel_array_remove_ordered(&ms->items, i);
                break;
            }
        }
    }
    item_free_recursive(item.h);
    return MEL_TRAY_OK;
}

bool mel_tray_item_alive(Mel_Tray_Item item) { return mel_tray__item_slot(item.h) != NULL; }
bool mel_tray_item_equal(Mel_Tray_Item a, Mel_Tray_Item b) { return a.h.index == b.h.index && a.h.generation == b.h.generation; }

Mel_Tray_Status mel_tray_item_set_label(Mel_Tray_Item item, str8 label)
{
    Item_Slot* is = mel_tray__item_slot(item.h);
    if (is == NULL)
    {
        mel_log_error("tray", "set_label on dead item");
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_DEAD_HANDLE;
    }
    str_free_local(&is->label);
    is->label = str_dup_local(label);
    Mel_Tray_Provider_Desc* prov = mel_tray__active_provider_desc();
    if (prov != NULL && prov->item_set_label != NULL)
        return prov->item_set_label(prov->user, mel_slotmap_handle_pack64(item.h), is->label);
    return MEL_TRAY_OK;
}

static Mel_Tray_Status item_apply_flags(Item_Slot* is, Mel_Tray_Item item)
{
    Mel_Tray_Provider_Desc* prov = mel_tray__active_provider_desc();
    if (prov != NULL && prov->item_set_flags != NULL)
        return prov->item_set_flags(prov->user, mel_slotmap_handle_pack64(item.h), is->flags);
    return MEL_TRAY_OK;
}

Mel_Tray_Status mel_tray_item_set_enabled(Mel_Tray_Item item, bool enabled)
{
    Item_Slot* is = mel_tray__item_slot(item.h);
    if (is == NULL)
    {
        mel_log_error("tray", "set_enabled on dead item");
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_DEAD_HANDLE;
    }
    if (enabled)
        is->flags |= MEL_TRAY_ITEM_ENABLED;
    else
        is->flags &= ~MEL_TRAY_ITEM_ENABLED;
    return item_apply_flags(is, item);
}

Mel_Tray_Status mel_tray_item_set_checked(Mel_Tray_Item item, bool checked)
{
    Item_Slot* is = mel_tray__item_slot(item.h);
    if (is == NULL)
    {
        mel_log_error("tray", "set_checked on dead item");
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_DEAD_HANDLE;
    }
    if ((is->flags & MEL_TRAY_ITEM_CHECKBOX) == 0)
    {
        mel_log_error("tray", "set_checked on non-checkbox item");
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_INVALID_ARG;
    }
    if (checked)
        is->flags |= MEL_TRAY_ITEM_CHECKED;
    else
        is->flags &= ~MEL_TRAY_ITEM_CHECKED;
    return item_apply_flags(is, item);
}

Mel_Tray_Status mel_tray_item_set_callback(Mel_Tray_Item item, Mel_Tray_Item_Callback cb, void* user)
{
    Item_Slot* is = mel_tray__item_slot(item.h);
    if (is == NULL)
    {
        mel_log_error("tray", "set_callback on dead item");
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_DEAD_HANDLE;
    }
    is->on_activate = cb;
    is->user = user;
    return MEL_TRAY_OK;
}

bool mel_tray_item_enabled(Mel_Tray_Item item)
{
    Item_Slot* is = mel_tray__item_slot(item.h);
    return is != NULL && (is->flags & MEL_TRAY_ITEM_ENABLED) != 0;
}

bool mel_tray_item_checked(Mel_Tray_Item item)
{
    Item_Slot* is = mel_tray__item_slot(item.h);
    return is != NULL && (is->flags & MEL_TRAY_ITEM_CHECKED) != 0;
}

Mel_Tray_Item_Flags mel_tray_item_flags(Mel_Tray_Item item)
{
    Item_Slot* is = mel_tray__item_slot(item.h);
    return is != NULL ? is->flags : 0;
}

Mel_Tray_Menu mel_tray_item_submenu(Mel_Tray_Item item)
{
    Item_Slot* is = mel_tray__item_slot(item.h);
    if (is == NULL || !mel_slotmap_handle_valid(is->submenu))
        return MEL_TRAY_MENU_NULL;
    return (Mel_Tray_Menu){ is->submenu };
}
