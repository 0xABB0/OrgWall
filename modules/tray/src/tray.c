#include <tray/tray.h>
#include <tray/menu.h>
#include <tray/events.h>
#include <tray/provider.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.slotmap/slotmap.h>
#include <collection.array/array.h>
#include <event/event.h>
#include <log/log.h>

#include <string.h>

#include "tray_internal.h"

#define MEL_TRAY_EVENTS_CAP 128

typedef struct
{
    Mel_Tray_Provider_Desc desc;
    u32                    generation;
    bool                   active;
} Provider_Entry;

typedef struct
{
    bool             initialized;
    const Mel_Alloc* alloc;
    Mel_Executor*    exec;

    Mel_SlotMap trays;
    Mel_SlotMap menus;
    Mel_SlotMap items;

    Mel_Array(Provider_Entry) providers;
    u32 provider_gen;
    i32 active_provider;

    Mel_Event*    events;
    Mel_Event_Sub poll_sub;
} Tray_Reg;

static Tray_Reg g;

Mel_SlotMap* mel_tray__trays(void) { return &g.trays; }
Mel_SlotMap* mel_tray__menus(void) { return &g.menus; }
Mel_SlotMap* mel_tray__items(void) { return &g.items; }

Tray_Slot*       mel_tray__slot(Mel_SlotMap_Handle h) { return (Tray_Slot*)mel_slotmap_get(&g.trays, h); }
Menu_Slot*       mel_tray__menu_slot(Mel_SlotMap_Handle h) { return (Menu_Slot*)mel_slotmap_get(&g.menus, h); }
Item_Slot*       mel_tray__item_slot(Mel_SlotMap_Handle h) { return (Item_Slot*)mel_slotmap_get(&g.items, h); }
const Mel_Alloc* mel_tray__alloc(void) { return g.alloc; }

static Provider_Entry* active_provider(void)
{
    if (g.active_provider >= 0)
    {
        Provider_Entry* pe = &g.providers.items[g.active_provider];
        if (pe->active)
            return pe;
        g.active_provider = -1;
    }
    for (u32 i = 0; i < g.providers.count; i++)
    {
        Provider_Entry* pe = &g.providers.items[i];
        if (!pe->active)
            continue;
        if (pe->desc.supported == NULL || pe->desc.supported(pe->desc.user))
        {
            g.active_provider = (i32)i;
            return pe;
        }
    }
    return NULL;
}

Mel_Tray_Provider_Desc* mel_tray__active_provider_desc(void)
{
    Provider_Entry* pe = active_provider();
    return pe != NULL ? &pe->desc : NULL;
}

static str8 str_dup(str8 s)
{
    if (s.len == 0 || s.data == NULL)
        return STR8_EMPTY;
    return str8_dup_alloc(s, g.alloc);
}

static void str_free(str8* s)
{
    if (s->data != NULL)
        mel_dealloc(g.alloc, s->data);
    *s = STR8_EMPTY;
}

static Mel_Tray_Image image_dup(Mel_Tray_Image src, Mel_Tray_Status* warn)
{
    Mel_Tray_Image out = { 0 };
    out.template_mask = src.template_mask;
    out.path = str_dup(src.path);
    if (src.rgba != NULL && src.width > 0 && src.height > 0)
    {
        usize bytes = (usize)src.width * (usize)src.height * 4u;
        u8*   buf = mel_alloc(g.alloc, bytes);
        if (buf != NULL)
        {
            memcpy(buf, src.rgba, bytes);
            out.rgba = buf;
            out.width = src.width;
            out.height = src.height;
        }
        else if (warn != NULL)
            *warn |= MEL_TRAY_ERR_BACKEND_FAIL;
    }
    return out;
}

static void image_free(Mel_Tray_Image* img)
{
    if (img->rgba != NULL)
        mel_dealloc(g.alloc, (void*)img->rgba);
    str_free(&img->path);
    *img = (Mel_Tray_Image){ 0 };
}

static void overflow_report(const Mel_Event_Overflow_Info* info, void* user)
{
    MEL_UNUSED(user);
    mel_log_warn("tray", "event channel full (capacity %u); dropping oldest, total_lagged=%llu", info->ring_capacity, (unsigned long long)info->total_lagged);
}

static void fire_event(Mel_Tray_Event ev)
{
    if (g.events != NULL)
        mel_event_fire(g.events, &ev);
}

Mel_Tray_Provider mel_tray_provider_register(const Mel_Tray_Provider_Desc* desc)
{
    Provider_Entry e = { .desc = *desc, .generation = ++g.provider_gen, .active = true };
    u32            idx = (u32)g.providers.count;
    mel_array_push(&g.providers, e);
    return (Mel_Tray_Provider){ .index = idx, .generation = e.generation };
}

void mel_tray_provider_unregister(Mel_Tray_Provider p)
{
    if (p.index < g.providers.count && g.providers.items[p.index].generation == p.generation)
    {
        g.providers.items[p.index].active = false;
        if (g.active_provider == (i32)p.index)
            g.active_provider = -1;
    }
}

void mel_tray__force_provider(Mel_Tray_Provider p)
{
    if (p.index < g.providers.count && g.providers.items[p.index].generation == p.generation && g.providers.items[p.index].active)
        g.active_provider = (i32)p.index;
}

void mel_tray__init_bare(const Mel_Alloc* alloc, Mel_Executor* exec)
{
    if (g.initialized)
        return;
    g.alloc = alloc ? alloc : mel_alloc_heap();
    g.exec = exec;
    g.active_provider = -1;
    g.provider_gen = 0;
    mel_slotmap_init(&g.trays, g.alloc, .item_size = sizeof(Tray_Slot), .initial_capacity = 2);
    mel_slotmap_init(&g.menus, g.alloc, .item_size = sizeof(Menu_Slot), .initial_capacity = 4);
    mel_slotmap_init(&g.items, g.alloc, .item_size = sizeof(Item_Slot), .initial_capacity = 8);
    mel_array_init(&g.providers, g.alloc);
    g.events = mel_event_create(g.alloc, sizeof(Mel_Tray_Event), MEL_TRAY_EVENTS_CAP, mel_event_policy_latest(overflow_report, NULL));
    g.poll_sub = g.events != NULL ? mel_event_subscribe_pull(g.events, NULL) : MEL_EVENT_SUB_NULL;
    g.initialized = true;
}

void mel_tray_init(const Mel_Alloc* alloc, Mel_Executor* exec)
{
    if (g.initialized)
        return;
    mel_tray__init_bare(alloc, exec);
    mel_tray__register_host_providers();
}

static void tray_destroy_internal(Mel_SlotMap_Handle th)
{
    Tray_Slot* ts = mel_tray__slot(th);
    if (ts == NULL)
        return;
    Provider_Entry* prov = active_provider();
    if (mel_slotmap_handle_valid(ts->menu))
        mel_tray__menu_free_recursive(ts->menu);
    if (prov != NULL && prov->desc.destroy != NULL)
        prov->desc.destroy(prov->desc.user, mel_slotmap_handle_pack64(th));
    image_free(&ts->image);
    str_free(&ts->tooltip);
    str_free(&ts->title);
    mel_slotmap_remove(&g.trays, th);
}

void mel_tray_shutdown(void)
{
    if (!g.initialized)
        return;
    Mel_Array(Mel_SlotMap_Handle) live;
    mel_array_init(&live, g.alloc);
    for (u32 i = 0; i < g.trays.slot_count; i++)
    {
        Mel_SlotMap_Handle h = mel_slotmap_handle_make(i, g.trays.slots[i].generation);
        if (mel_slotmap_alive(&g.trays, h))
            mel_array_push(&live, h);
    }
    for (usize i = 0; i < live.count; i++)
        tray_destroy_internal(live.items[i]);
    mel_array_free(&live);
    if (g.events != NULL)
        mel_event_unsubscribe(g.events, g.poll_sub);
    mel_event_destroy(g.events);
    mel_array_free(&g.providers);
    mel_slotmap_free(&g.items);
    mel_slotmap_free(&g.menus);
    mel_slotmap_free(&g.trays);
    memset(&g, 0, sizeof g);
}

bool mel_tray_supported(void)
{
    if (!g.initialized)
        return false;
    return active_provider() != NULL;
}

Mel_Tray_Create_Result mel_tray_create_opt(Mel_Tray_Opt opt)
{
    Mel_Tray_Create_Result r = { .value = MEL_TRAY_NULL, .status = MEL_TRAY_OK };
    if (!g.initialized)
        mel_tray_init(opt.alloc, opt.exec);

    Provider_Entry* prov = active_provider();
    if (prov == NULL || prov->desc.create == NULL)
    {
        mel_log_error("tray", "create with no active provider");
        r.status = MEL_TRAY_ERROR | MEL_TRAY_ERR_NO_PROVIDER;
        return r;
    }

    Menu_Slot ms = { 0 };
    mel_array_init(&ms.items, g.alloc);
    Mel_SlotMap_Handle menu_h = mel_slotmap_insert(&g.menus, &ms);

    Mel_Tray_Status warn = 0;
    Tray_Slot       ts = { 0 };
    ts.menu = menu_h;
    ts.image = image_dup(opt.image, &warn);
    ts.tooltip = str_dup(opt.tooltip);
    ts.title = str_dup(opt.title);
    ts.visible = opt.visible;
    Mel_SlotMap_Handle th = mel_slotmap_insert(&g.trays, &ts);

    Menu_Slot* msp = mel_tray__menu_slot(menu_h);
    msp->tray = th;

    if (prov->desc.menu_create != NULL)
    {
        Mel_Tray_Status s = prov->desc.menu_create(prov->desc.user, mel_slotmap_handle_pack64(menu_h));
        if (mel_tray_failed(s))
        {
            mel_log_error("tray", "provider '%s' menu_create failed", prov->desc.name ? prov->desc.name : "?");
            tray_destroy_internal(th);
            r.status = s;
            return r;
        }
        warn |= (s & ~MEL_TRAY_SEVERITY_MASK);
    }

    Mel_Tray_Lowered lowered = {
        .token = mel_slotmap_handle_pack64(th),
        .image = ts.image,
        .tooltip = ts.tooltip,
        .title = ts.title,
        .visible = ts.visible,
        .menu_token = mel_slotmap_handle_pack64(menu_h),
    };
    Mel_Tray_Status cs = prov->desc.create(prov->desc.user, &lowered);
    if (mel_tray_failed(cs))
    {
        mel_log_error("tray", "provider '%s' create failed", prov->desc.name ? prov->desc.name : "?");
        tray_destroy_internal(th);
        r.status = cs;
        return r;
    }
    warn |= (cs & ~MEL_TRAY_SEVERITY_MASK);

    r.value = (Mel_Tray){ th };
    r.status = warn ? (MEL_TRAY_WARNED | warn) : MEL_TRAY_OK;
    return r;
}

void mel_tray_destroy(Mel_Tray t)
{
    if (!g.initialized)
        return;
    if (!mel_slotmap_alive(&g.trays, t.h))
    {
        mel_log_error("tray", "destroy on dead handle {index=%u, gen=%u}", t.h.index, t.h.generation);
        return;
    }
    tray_destroy_internal(t.h);
}

bool mel_tray_alive(Mel_Tray t) { return g.initialized && mel_slotmap_alive(&g.trays, t.h); }
bool mel_tray_equal(Mel_Tray a, Mel_Tray b) { return a.h.index == b.h.index && a.h.generation == b.h.generation; }

Mel_Tray_Status mel_tray_set_image(Mel_Tray t, Mel_Tray_Image image)
{
    Tray_Slot* ts = g.initialized ? mel_tray__slot(t.h) : NULL;
    if (ts == NULL)
    {
        mel_log_error("tray", "set_image on dead handle");
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_DEAD_HANDLE;
    }
    Provider_Entry* prov = active_provider();
    Mel_Tray_Status warn = 0;
    image_free(&ts->image);
    ts->image = image_dup(image, &warn);
    if (prov != NULL && prov->desc.set_image != NULL)
    {
        Mel_Tray_Status s = prov->desc.set_image(prov->desc.user, mel_slotmap_handle_pack64(t.h), ts->image);
        if (mel_tray_failed(s))
            return s;
        warn |= (s & ~MEL_TRAY_SEVERITY_MASK);
    }
    return warn ? (MEL_TRAY_WARNED | warn) : MEL_TRAY_OK;
}

Mel_Tray_Status mel_tray_set_tooltip(Mel_Tray t, str8 tooltip)
{
    Tray_Slot* ts = g.initialized ? mel_tray__slot(t.h) : NULL;
    if (ts == NULL)
    {
        mel_log_error("tray", "set_tooltip on dead handle");
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_DEAD_HANDLE;
    }
    str_free(&ts->tooltip);
    ts->tooltip = str_dup(tooltip);
    Provider_Entry* prov = active_provider();
    if (prov != NULL && prov->desc.set_tooltip != NULL)
        return prov->desc.set_tooltip(prov->desc.user, mel_slotmap_handle_pack64(t.h), ts->tooltip);
    return MEL_TRAY_OK;
}

Mel_Tray_Status mel_tray_set_title(Mel_Tray t, str8 title)
{
    Tray_Slot* ts = g.initialized ? mel_tray__slot(t.h) : NULL;
    if (ts == NULL)
    {
        mel_log_error("tray", "set_title on dead handle");
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_DEAD_HANDLE;
    }
    str_free(&ts->title);
    ts->title = str_dup(title);
    Provider_Entry* prov = active_provider();
    if (prov != NULL && prov->desc.set_title != NULL)
        return prov->desc.set_title(prov->desc.user, mel_slotmap_handle_pack64(t.h), ts->title);
    return MEL_TRAY_OK;
}

Mel_Tray_Status mel_tray_set_visible(Mel_Tray t, bool visible)
{
    Tray_Slot* ts = g.initialized ? mel_tray__slot(t.h) : NULL;
    if (ts == NULL)
    {
        mel_log_error("tray", "set_visible on dead handle");
        return MEL_TRAY_ERROR | MEL_TRAY_ERR_DEAD_HANDLE;
    }
    ts->visible = visible;
    Provider_Entry* prov = active_provider();
    if (prov != NULL && prov->desc.set_visible != NULL)
        return prov->desc.set_visible(prov->desc.user, mel_slotmap_handle_pack64(t.h), visible);
    return MEL_TRAY_OK;
}

bool mel_tray_visible(Mel_Tray t)
{
    Tray_Slot* ts = g.initialized ? mel_tray__slot(t.h) : NULL;
    return ts != NULL && ts->visible;
}

Mel_Tray_Menu mel_tray_menu(Mel_Tray t)
{
    Tray_Slot* ts = g.initialized ? mel_tray__slot(t.h) : NULL;
    if (ts == NULL)
        return MEL_TRAY_MENU_NULL;
    return (Mel_Tray_Menu){ ts->menu };
}

void* mel_tray_native(Mel_Tray t)
{
    Tray_Slot* ts = g.initialized ? mel_tray__slot(t.h) : NULL;
    if (ts == NULL)
        return NULL;
    Provider_Entry* prov = active_provider();
    return (prov != NULL && prov->desc.native != NULL) ? prov->desc.native(prov->desc.user, mel_slotmap_handle_pack64(t.h)) : NULL;
}

u32 mel_tray_poll_events(Mel_Tray_Event* out, u32 cap)
{
    if (!g.initialized || g.events == NULL)
        return 0;
    u32 n = 0;
    for (; n < cap && mel_event_pull(g.events, g.poll_sub, &out[n]); n++)
        ;
    return n;
}

Mel_Tray_Subscription mel_tray_subscribe(Mel_Executor* exec, Mel_Tray_Event_Callback cb, void* user)
{
    if (!g.initialized || g.events == NULL)
    {
        mel_log_error("tray", "subscribe before init; no channel");
        return MEL_TRAY_SUBSCRIPTION_NULL;
    }
    Mel_Executor* target = exec != NULL ? exec : g.exec;
    if (target == NULL)
    {
        mel_log_error("tray", "subscribe needs an executor; none passed and registry has none (pull-only init)");
        return MEL_TRAY_SUBSCRIPTION_NULL;
    }
    Mel_Event_Sub sub = mel_event_subscribe_push(g.events, target, (Mel_Event_Callback)cb, user);
    return (Mel_Tray_Subscription){ sub.handle };
}

void mel_tray_unsubscribe(Mel_Tray_Subscription sub)
{
    if (!g.initialized || g.events == NULL)
        return;
    mel_event_unsubscribe(g.events, (Mel_Event_Sub){ sub.handle });
}

void mel_tray__dispatch_activate(u64 tray_token, Mel_Tray_Buttons buttons)
{
    if (!g.initialized)
        return;
    Mel_SlotMap_Handle th = mel_slotmap_handle_unpack64(tray_token);
    if (!mel_slotmap_alive(&g.trays, th))
        return;
    fire_event((Mel_Tray_Event){ .kind = MEL_TRAY_EVENT_ACTIVATED, .tray = { th }, .item = MEL_TRAY_ITEM_NULL, .buttons = buttons });
}

void mel_tray__dispatch_item_clicked(u64 item_token)
{
    if (!g.initialized)
        return;
    Mel_SlotMap_Handle ih = mel_slotmap_handle_unpack64(item_token);
    Item_Slot*         is = mel_tray__item_slot(ih);
    if (is == NULL)
        return;

    if ((is->flags & MEL_TRAY_ITEM_CHECKBOX) != 0)
    {
        is->flags ^= MEL_TRAY_ITEM_CHECKED;
        Provider_Entry* prov = active_provider();
        if (prov != NULL && prov->desc.item_set_flags != NULL)
            prov->desc.item_set_flags(prov->desc.user, item_token, is->flags);
    }

    Mel_Tray_Item_Callback cb = is->on_activate;
    void*                  user = is->user;
    Mel_SlotMap_Handle     th = is->tray;
    fire_event((Mel_Tray_Event){ .kind = MEL_TRAY_EVENT_ITEM_CLICKED, .tray = { th }, .item = { ih }, .buttons = MEL_TRAY_BUTTON_LEFT });
    if (cb != NULL)
        cb((Mel_Tray_Item){ ih }, user);
}

void mel_tray__dispatch_menu(u64 tray_token, Mel_Tray_Event_Kind kind)
{
    if (!g.initialized)
        return;
    Mel_SlotMap_Handle th = mel_slotmap_handle_unpack64(tray_token);
    if (!mel_slotmap_alive(&g.trays, th))
        return;
    fire_event((Mel_Tray_Event){ .kind = kind, .tray = { th }, .item = MEL_TRAY_ITEM_NULL, .buttons = 0 });
}
