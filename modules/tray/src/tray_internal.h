#pragma once

#include <tray/tray.h>
#include <tray/menu.h>
#include <tray/events.h>
#include <tray/provider.h>

#include <collection.slotmap/slotmap.h>
#include <collection.array/array.h>

typedef struct
{
    Mel_SlotMap_Handle menu;
    Mel_Tray_Image     image;
    str8               tooltip;
    str8               title;
    bool               visible;
} Tray_Slot;

typedef struct
{
    Mel_SlotMap_Handle tray;
    Mel_Array(Mel_SlotMap_Handle) items;
} Menu_Slot;

typedef struct
{
    Mel_SlotMap_Handle     tray;
    Mel_SlotMap_Handle     menu;
    Mel_SlotMap_Handle     submenu;
    str8                   label;
    Mel_Tray_Item_Flags    flags;
    Mel_Tray_Item_Callback on_activate;
    void*                  user;
} Item_Slot;

Mel_SlotMap* mel_tray__trays(void);
Mel_SlotMap* mel_tray__menus(void);
Mel_SlotMap* mel_tray__items(void);

Tray_Slot* mel_tray__slot(Mel_SlotMap_Handle h);
Menu_Slot* mel_tray__menu_slot(Mel_SlotMap_Handle h);
Item_Slot* mel_tray__item_slot(Mel_SlotMap_Handle h);

const Mel_Alloc*        mel_tray__alloc(void);
Mel_Tray_Provider_Desc* mel_tray__active_provider_desc(void);

void mel_tray__menu_free_recursive(Mel_SlotMap_Handle mh);

void mel_tray__force_provider(Mel_Tray_Provider p);
void mel_tray__init_bare(const Mel_Alloc* alloc, Mel_Executor* exec);
