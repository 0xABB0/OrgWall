#include <locale/locale.h>
#include <locale/events.h>
#include <locale/provider.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection.array/array.h>
#include <collection.slotmap/slotmap.fwd.h>
#include <event/event.h>
#include <log/log.h>

#include <string.h>

#include "locale_backend.h"

#define MEL_LOCALE_EVENTS_CAP 64

typedef struct
{
    Mel_Locale_Provider_Desc desc;
    u32                      generation;
    bool                     active;
    bool                     watching;
} Provider_Entry;

typedef Mel_Array(Mel_Locale) Locale_List;
typedef Mel_Array(Mel_Locale_Raw) Raw_List;
typedef Mel_Array(Provider_Entry) Provider_List;

typedef struct
{
    bool             initialized;
    const Mel_Alloc* alloc;
    Mel_Executor*    exec;

    Locale_List   list;
    Provider_List providers;

    Mel_Event*    events;
    Mel_Event_Sub poll_sub;

    u32 provider_gen;
} Registry;

static Registry g_reg;

static const Mel_Locale_Provider_Desc* g_host_override;

void mel_locale__set_host_provider_override(const Mel_Locale_Provider_Desc* desc) { g_host_override = desc; }

static void list_free(Locale_List* list)
{
    for (usize i = 0; i < list->count; i++)
    {
        if (list->items[i].tag.data)
            mel_dealloc(g_reg.alloc, list->items[i].tag.data);
    }
    mel_array_clear(list);
}

static bool is_ascii_alpha(u8 c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
static bool is_ascii_alnum(u8 c) { return is_ascii_alpha(c) || (c >= '0' && c <= '9'); }
static u8   to_lower(u8 c) { return (c >= 'A' && c <= 'Z') ? (u8)(c - 'A' + 'a') : c; }
static u8   to_upper(u8 c) { return (c >= 'a' && c <= 'z') ? (u8)(c - 'a' + 'A') : c; }

static bool parse_tag(str8 raw, Mel_Locale* out)
{
    size i = 0;
    while (i < raw.len && (raw.data[i] == ' ' || raw.data[i] == '\t'))
        i++;
    size lang_begin = i;
    while (i < raw.len && is_ascii_alpha(raw.data[i]))
        i++;
    size lang_len = i - lang_begin;
    if (lang_len < 2 || lang_len > 3)
        return false;

    size country_begin = 0;
    size country_len = 0;
    if (i < raw.len && (raw.data[i] == '-' || raw.data[i] == '_'))
    {
        size sep = i;
        i++;
        size sub_begin = i;
        while (i < raw.len && is_ascii_alnum(raw.data[i]))
            i++;
        size sub_len = i - sub_begin;
        if (sub_len == 2 && is_ascii_alpha(raw.data[sub_begin]) && is_ascii_alpha(raw.data[sub_begin + 1]))
        {
            country_begin = sub_begin;
            country_len = sub_len;
        }
        else
            (void)sep;
    }

    usize tag_len = (usize)lang_len + (country_len ? (usize)country_len + 1 : 0);
    u8*   buf = (u8*)mel_alloc(g_reg.alloc, tag_len);
    if (!buf)
        return false;

    usize w = 0;
    for (size k = 0; k < lang_len; k++)
        buf[w++] = to_lower(raw.data[lang_begin + k]);
    usize lang_w = w;
    if (country_len)
    {
        buf[w++] = '-';
        for (size k = 0; k < country_len; k++)
            buf[w++] = to_upper(raw.data[country_begin + k]);
    }

    out->tag = (str8){ .data = buf, .len = (size)tag_len };
    out->language = (str8){ .data = buf, .len = (size)lang_w };
    out->country = country_len ? (str8){ .data = buf + lang_w + 1, .len = (size)country_len } : STR8_EMPTY;
    return true;
}

static bool list_contains_tag(Locale_List* list, str8 tag)
{
    for (usize i = 0; i < list->count; i++)
        if (str8_equals(list->items[i].tag, tag))
            return true;
    return false;
}

static u32 diff_fields(Locale_List* old, Locale_List* fresh)
{
    u32 f = 0;
    if (old->count != fresh->count)
        f |= MEL_LOCALE_FIELD_MEMBERSHIP;
    else
    {
        for (usize i = 0; i < fresh->count; i++)
            if (!list_contains_tag(old, fresh->items[i].tag))
            {
                f |= MEL_LOCALE_FIELD_MEMBERSHIP;
                break;
            }
    }

    bool primary_changed = (old->count == 0) != (fresh->count == 0);
    if (!primary_changed && old->count > 0 && fresh->count > 0)
        primary_changed = !str8_equals(old->items[0].tag, fresh->items[0].tag);
    if (primary_changed)
        f |= MEL_LOCALE_FIELD_PRIMARY;

    if (!(f & MEL_LOCALE_FIELD_MEMBERSHIP) && old->count == fresh->count)
    {
        for (usize i = 0; i < fresh->count; i++)
            if (!str8_equals(old->items[i].tag, fresh->items[i].tag))
            {
                f |= MEL_LOCALE_FIELD_ORDER;
                break;
            }
    }
    return f;
}

static void events_overflow_report(const Mel_Event_Overflow_Info* info, void* user)
{
    (void)user;
    mel_log_warn("locale", "event channel full (capacity %u); dropping oldest, total_lagged=%llu", info->ring_capacity, (unsigned long long)info->total_lagged);
}

static void fire_event(u32 changed_fields)
{
    if (g_reg.events == NULL)
        return;
    Mel_Locale_Event ev = { .changed_fields = changed_fields };
    mel_event_fire(g_reg.events, &ev);
}

Mel_Locale_Provider mel_locale_provider_register(const Mel_Locale_Provider_Desc* desc)
{
    Provider_Entry e = { .desc = *desc, .generation = ++g_reg.provider_gen, .active = true };
    u32            idx = (u32)g_reg.providers.count;
    mel_array_push(&g_reg.providers, e);
    Provider_Entry* pe = &g_reg.providers.items[idx];
    if (pe->desc.watch)
    {
        pe->desc.watch(pe->desc.user, mel_locale__on_change, NULL);
        pe->watching = true;
    }
    return (Mel_Locale_Provider){ .index = idx, .generation = e.generation };
}

void mel_locale_provider_unregister(Mel_Locale_Provider p)
{
    if (p.index < g_reg.providers.count && g_reg.providers.items[p.index].generation == p.generation)
    {
        Provider_Entry* pe = &g_reg.providers.items[p.index];
        if (pe->watching && pe->desc.unwatch)
            pe->desc.unwatch(pe->desc.user);
        pe->watching = false;
        pe->active = false;
    }
}

void mel_locale_init_ex(const Mel_Alloc* alloc, Mel_Executor* exec)
{
    if (g_reg.initialized)
        return;
    g_reg.alloc = alloc ? alloc : mel_alloc_heap();
    g_reg.exec = exec;
    mel_array_init(&g_reg.list, g_reg.alloc);
    mel_array_init(&g_reg.providers, g_reg.alloc);
    g_reg.provider_gen = 0;

    g_reg.events = mel_event_create(g_reg.alloc, sizeof(Mel_Locale_Event), MEL_LOCALE_EVENTS_CAP, mel_event_policy_latest(events_overflow_report, NULL));
    g_reg.poll_sub = g_reg.events != NULL ? mel_event_subscribe_pull(g_reg.events, NULL) : MEL_EVENT_SUB_NULL;

    g_reg.initialized = true;
    if (g_host_override != NULL)
        mel_locale_provider_register(g_host_override);
    else
        mel_locale__register_host_providers();
    mel_locale_refresh();
}

void mel_locale_init(const Mel_Alloc* alloc) { mel_locale_init_ex(alloc, NULL); }

void mel_locale_shutdown(void)
{
    if (!g_reg.initialized)
        return;
    for (usize i = 0; i < g_reg.providers.count; i++)
    {
        Provider_Entry* pe = &g_reg.providers.items[i];
        if (pe->active && pe->watching && pe->desc.unwatch)
            pe->desc.unwatch(pe->desc.user);
    }
    if (g_reg.events != NULL)
        mel_event_unsubscribe(g_reg.events, g_reg.poll_sub);
    mel_event_destroy(g_reg.events);
    list_free(&g_reg.list);
    mel_array_free(&g_reg.list);
    mel_array_free(&g_reg.providers);
    memset(&g_reg, 0, sizeof g_reg);
}

u32 mel_locale_refresh(void)
{
    if (!g_reg.initialized)
        mel_locale_init(NULL);

    Locale_List fresh;
    mel_array_init(&fresh, g_reg.alloc);

    for (usize pi = 0; pi < g_reg.providers.count; pi++)
    {
        Provider_Entry* pe = &g_reg.providers.items[pi];
        if (!pe->active || !pe->desc.enumerate)
            continue;
        Raw_List raw;
        mel_array_init(&raw, g_reg.alloc);
        mel_array_reserve(&raw, 8);
        u32 n = pe->desc.enumerate(pe->desc.user, g_reg.alloc, raw.items, (u32)raw.capacity);
        while (n > raw.capacity)
        {
            mel_array_reserve(&raw, n);
            n = pe->desc.enumerate(pe->desc.user, g_reg.alloc, raw.items, (u32)raw.capacity);
        }
        for (u32 i = 0; i < n; i++)
        {
            Mel_Locale parsed;
            bool       ok = parse_tag(raw.items[i].tag, &parsed);
            if (!ok)
                mel_log_warn("locale", "provider '%s' yielded unparseable tag (len=%lld)", pe->desc.name ? pe->desc.name : "?", (long long)raw.items[i].tag.len);
            if (raw.items[i].tag.data)
                mel_dealloc(g_reg.alloc, raw.items[i].tag.data);
            if (!ok)
                continue;
            if (!list_contains_tag(&fresh, parsed.tag))
                mel_array_push(&fresh, parsed);
            else
                mel_dealloc(g_reg.alloc, parsed.tag.data);
        }
        mel_array_free(&raw);
        if (fresh.count > 0)
            break;
    }

    u32 fields = diff_fields(&g_reg.list, &fresh);

    list_free(&g_reg.list);
    for (usize i = 0; i < fresh.count; i++)
        mel_array_push(&g_reg.list, fresh.items[i]);
    mel_array_free(&fresh);

    if (fields != 0)
        fire_event(fields);

    return (u32)g_reg.list.count;
}

void mel_locale__on_change(void* core)
{
    (void)core;
    if (g_reg.initialized)
        mel_locale_refresh();
}

u32 mel_locale_count(void) { return g_reg.initialized ? (u32)g_reg.list.count : 0; }

u32 mel_locale_list(Mel_Locale* out, u32 cap)
{
    if (!g_reg.initialized)
        return 0;
    u32 n = g_reg.list.count < cap ? (u32)g_reg.list.count : cap;
    for (u32 i = 0; i < n; i++)
        out[i] = g_reg.list.items[i];
    return n;
}

Mel_Locale_Get_Result mel_locale_at(u32 index)
{
    Mel_Locale_Get_Result r = { .value = { 0 }, .status = MEL_LOCALE_ERROR | MEL_LOCALE_OUT_OF_RANGE };
    if (!g_reg.initialized || index >= g_reg.list.count)
    {
        mel_log_error("locale", "at(%u) out of range (count=%u)", index, mel_locale_count());
        return r;
    }
    r.value = g_reg.list.items[index];
    r.status = mel_locale_has_country(r.value) ? MEL_LOCALE_OK : (MEL_LOCALE_OK | MEL_LOCALE_NO_COUNTRY);
    return r;
}

Mel_Locale_Get_Result mel_locale_primary(void)
{
    if (g_reg.initialized && g_reg.list.count == 0)
    {
        Mel_Locale_Get_Result r = { .value = { 0 }, .status = MEL_LOCALE_ERROR | MEL_LOCALE_EMPTY };
        mel_log_error("locale", "primary() requested but preferred list is empty");
        return r;
    }
    return mel_locale_at(0);
}

bool mel_locale_equal(Mel_Locale a, Mel_Locale b) { return str8_equals(a.tag, b.tag); }

u32 mel_locale_poll_events(Mel_Locale_Event* out, u32 cap)
{
    if (!g_reg.initialized || g_reg.events == NULL)
        return 0;
    u32 n = 0;
    for (; n < cap && mel_event_pull(g_reg.events, g_reg.poll_sub, &out[n]); n++)
        ;
    return n;
}

Mel_Locale_Subscription mel_locale_subscribe(Mel_Executor* exec, Mel_Locale_Event_Callback cb, void* user)
{
    if (!g_reg.initialized || g_reg.events == NULL)
    {
        mel_log_error("locale", "subscribe before init; no channel");
        return MEL_LOCALE_SUBSCRIPTION_NULL;
    }
    Mel_Executor* target = exec != NULL ? exec : g_reg.exec;
    if (target == NULL)
    {
        mel_log_error("locale", "subscribe needs an executor; none passed and registry has none (pull-only init)");
        return MEL_LOCALE_SUBSCRIPTION_NULL;
    }
    Mel_Event_Sub sub = mel_event_subscribe_push(g_reg.events, target, (Mel_Event_Callback)cb, user);
    return (Mel_Locale_Subscription){ sub.handle };
}

void mel_locale_unsubscribe(Mel_Locale_Subscription sub)
{
    if (!g_reg.initialized || g_reg.events == NULL)
        return;
    mel_event_unsubscribe(g_reg.events, (Mel_Event_Sub){ sub.handle });
}
