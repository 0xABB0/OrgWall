#pragma once

#include <tray/tray.h>
#include <tray/menu.h>
#include <tray/events.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    u64            token;
    Mel_Tray_Image image;
    str8           tooltip;
    str8           title;
    bool           visible;
    u64            menu_token;
} Mel_Tray_Lowered;

typedef struct
{
    u64                 token;
    u64                 parent_menu_token;
    u32                 at;
    str8                label;
    Mel_Tray_Item_Flags flags;
    u64                 submenu_token;
} Mel_Tray_Item_Lowered;

typedef struct
{
    const char* name;
    void*       user;

    bool (*supported)(void* user);

    Mel_Tray_Status (*create)(void* user, const Mel_Tray_Lowered* lowered);
    void (*destroy)(void* user, u64 token);

    Mel_Tray_Status (*set_image)(void* user, u64 token, Mel_Tray_Image image);
    Mel_Tray_Status (*set_tooltip)(void* user, u64 token, str8 tooltip);
    Mel_Tray_Status (*set_title)(void* user, u64 token, str8 title);
    Mel_Tray_Status (*set_visible)(void* user, u64 token, bool visible);

    Mel_Tray_Status (*menu_create)(void* user, u64 menu_token);
    void (*menu_destroy)(void* user, u64 menu_token);
    Mel_Tray_Status (*item_add)(void* user, const Mel_Tray_Item_Lowered* lowered);
    void (*item_remove)(void* user, u64 token);
    Mel_Tray_Status (*item_set_label)(void* user, u64 token, str8 label);
    Mel_Tray_Status (*item_set_flags)(void* user, u64 token, Mel_Tray_Item_Flags flags);

    void* (*native)(void* user, u64 token);
} Mel_Tray_Provider_Desc;

typedef struct
{
    u32 index;
    u32 generation;
} Mel_Tray_Provider;

Mel_Tray_Provider mel_tray_provider_register(const Mel_Tray_Provider_Desc* desc);
void              mel_tray_provider_unregister(Mel_Tray_Provider p);

void mel_tray__register_host_providers(void);

void mel_tray__dispatch_activate(u64 tray_token, Mel_Tray_Buttons buttons);
void mel_tray__dispatch_item_clicked(u64 item_token);
void mel_tray__dispatch_menu(u64 tray_token, Mel_Tray_Event_Kind kind);

#ifdef __cplusplus
}
#endif
