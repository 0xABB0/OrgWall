#pragma once

#include <tray/tray.h>

#ifdef __cplusplus
extern "C"
{
#endif

enum
{
    MEL_TRAY_ITEM_BUTTON = 1u << 0,
    MEL_TRAY_ITEM_CHECKBOX = 1u << 1,
    MEL_TRAY_ITEM_SEPARATOR = 1u << 2,
    MEL_TRAY_ITEM_SUBMENU = 1u << 3,

    MEL_TRAY_ITEM_KIND_MASK = MEL_TRAY_ITEM_BUTTON | MEL_TRAY_ITEM_CHECKBOX | MEL_TRAY_ITEM_SEPARATOR | MEL_TRAY_ITEM_SUBMENU,

    MEL_TRAY_ITEM_ENABLED = 1u << 8,
    MEL_TRAY_ITEM_CHECKED = 1u << 9,
};

typedef u32 Mel_Tray_Item_Flags;

typedef void (*Mel_Tray_Item_Callback)(Mel_Tray_Item item, void* user);

typedef struct
{
    str8                   label;
    Mel_Tray_Item_Flags    flags;
    Mel_Tray_Item_Callback on_activate;
    void*                  user;
} Mel_Tray_Item_Desc;

typedef struct
{
    Mel_Tray_Item   value;
    Mel_Tray_Status status;
} Mel_Tray_Item_Result;

typedef struct
{
    Mel_Tray_Menu   value;
    Mel_Tray_Status status;
} Mel_Tray_Submenu_Result;

bool mel_tray_menu_alive(Mel_Tray_Menu m);
bool mel_tray_menu_equal(Mel_Tray_Menu a, Mel_Tray_Menu b);
u32  mel_tray_menu_count(Mel_Tray_Menu m);

Mel_Tray_Item_Result mel_tray_item_add(Mel_Tray_Menu m, Mel_Tray_Item_Desc desc);
Mel_Tray_Item_Result mel_tray_item_insert(Mel_Tray_Menu m, u32 at, Mel_Tray_Item_Desc desc);
Mel_Tray_Status      mel_tray_item_remove(Mel_Tray_Item item);

Mel_Tray_Item_Result    mel_tray_separator_add(Mel_Tray_Menu m);
Mel_Tray_Submenu_Result mel_tray_submenu_add(Mel_Tray_Menu m, str8 label);
Mel_Tray_Submenu_Result mel_tray_submenu_insert(Mel_Tray_Menu m, u32 at, str8 label);

bool mel_tray_item_alive(Mel_Tray_Item item);
bool mel_tray_item_equal(Mel_Tray_Item a, Mel_Tray_Item b);

Mel_Tray_Status mel_tray_item_set_label(Mel_Tray_Item item, str8 label);
Mel_Tray_Status mel_tray_item_set_enabled(Mel_Tray_Item item, bool enabled);
Mel_Tray_Status mel_tray_item_set_checked(Mel_Tray_Item item, bool checked);
Mel_Tray_Status mel_tray_item_set_callback(Mel_Tray_Item item, Mel_Tray_Item_Callback cb, void* user);

bool                mel_tray_item_enabled(Mel_Tray_Item item);
bool                mel_tray_item_checked(Mel_Tray_Item item);
Mel_Tray_Item_Flags mel_tray_item_flags(Mel_Tray_Item item);
Mel_Tray_Menu       mel_tray_item_submenu(Mel_Tray_Item item);

#ifdef __cplusplus
}
#endif
