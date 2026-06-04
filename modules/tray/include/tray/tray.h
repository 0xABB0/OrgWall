#pragma once

#include <core/types.h>
#include <core/compiler.h>
#include <string/str8.h>
#include <allocator/allocator.fwd.h>
#include <collection.slotmap/slotmap.fwd.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Executor Mel_Executor;

typedef u32 Mel_Tray_Status;

#define MEL_TRAY_SEVERITY_MASK          0x3u
#define MEL_TRAY_OK                     0u
#define MEL_TRAY_WARNED                 1u
#define MEL_TRAY_ERROR                  2u

#define MEL_TRAY_WARN_IMAGE_RESCALED    (1u << 2)
#define MEL_TRAY_WARN_TOOLTIP_DROPPED   (1u << 3)
#define MEL_TRAY_WARN_TITLE_DROPPED     (1u << 4)
#define MEL_TRAY_WARN_SUBMENU_FLATTENED (1u << 5)

#define MEL_TRAY_ERR_NO_PROVIDER        (1u << 6)
#define MEL_TRAY_ERR_INVALID_ARG        (1u << 7)
#define MEL_TRAY_ERR_DEAD_HANDLE        (1u << 8)
#define MEL_TRAY_ERR_BACKEND_FAIL       (1u << 9)

static inline bool mel_tray_failed(Mel_Tray_Status s) { return (s & MEL_TRAY_SEVERITY_MASK) == MEL_TRAY_ERROR; }
static inline bool mel_tray_warned(Mel_Tray_Status s) { return (s & MEL_TRAY_SEVERITY_MASK) == MEL_TRAY_WARNED; }

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_Tray;

#define MEL_TRAY_NULL ((Mel_Tray){ 0 })

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_Tray_Menu;

#define MEL_TRAY_MENU_NULL ((Mel_Tray_Menu){ 0 })

typedef struct
{
    Mel_SlotMap_Handle h;
} Mel_Tray_Item;

#define MEL_TRAY_ITEM_NULL ((Mel_Tray_Item){ 0 })

typedef struct
{
    const u8* rgba;
    u32       width;
    u32       height;
    str8      path;
    bool      template_mask;
} Mel_Tray_Image;

typedef struct
{
    Mel_Tray_Image   image;
    str8             tooltip;
    str8             title;
    bool             visible;
    const Mel_Alloc* alloc;
    Mel_Executor*    exec;
} Mel_Tray_Opt;

typedef struct
{
    Mel_Tray        value;
    Mel_Tray_Status status;
} Mel_Tray_Create_Result;

void mel_tray_init(const Mel_Alloc* alloc, Mel_Executor* exec);
void mel_tray_shutdown(void);

bool mel_tray_supported(void);

Mel_Tray_Create_Result mel_tray_create_opt(Mel_Tray_Opt opt);
#define mel_tray_create(...) mel_tray_create_opt((Mel_Tray_Opt){ .visible = true, __VA_ARGS__ })

void mel_tray_destroy(Mel_Tray t);

bool mel_tray_alive(Mel_Tray t);
bool mel_tray_equal(Mel_Tray a, Mel_Tray b);

Mel_Tray_Status mel_tray_set_image(Mel_Tray t, Mel_Tray_Image image);
Mel_Tray_Status mel_tray_set_tooltip(Mel_Tray t, str8 tooltip);
Mel_Tray_Status mel_tray_set_title(Mel_Tray t, str8 title);
Mel_Tray_Status mel_tray_set_visible(Mel_Tray t, bool visible);

bool mel_tray_visible(Mel_Tray t);

Mel_Tray_Menu mel_tray_menu(Mel_Tray t);

void* mel_tray_native(Mel_Tray t);

#ifdef __cplusplus
}
#endif
