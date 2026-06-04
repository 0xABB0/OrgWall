#include <time/format_prefs.h>
#include <time/format_provider.h>

#include <allocator/allocator.h>
#include <collection.array/array.h>
#include <debug/assert.h>

#include <stdio.h>
#include <string.h>

#include "format_backend.h"

typedef struct
{
    Mel_Time_Format_Provider_Desc desc;
    u32                           generation;
    bool                          active;
} Provider_Entry;

typedef Mel_Array(Provider_Entry) Provider_List;

typedef struct
{
    bool                   initialized;
    const Mel_Alloc*       alloc;
    Provider_List          providers;
    u32                    provider_gen;
    Mel_Time_Format_Prefs  cached;
    Mel_Time_Format_Status status;
} Registry;

static Registry g_reg;

static const Mel_Time_Format_Provider_Desc* g_host_override;

void mel_time_format__set_host_provider_override(const Mel_Time_Format_Provider_Desc* desc) { g_host_override = desc; }

static bool order_is_singular(u32 order) { return order == MEL_DATE_ORDER_YMD || order == MEL_DATE_ORDER_DMY || order == MEL_DATE_ORDER_MDY; }

static bool clock_is_singular(u32 clock) { return clock == MEL_CLOCK_24H || clock == MEL_CLOCK_12H; }

Mel_Time_Format_Provider mel_time_format_provider_register(const Mel_Time_Format_Provider_Desc* desc)
{
    mel_assert(g_reg.initialized);
    mel_assert(desc != NULL && desc->query != NULL);
    Provider_Entry e = { .desc = *desc, .generation = ++g_reg.provider_gen, .active = true };
    u32            idx = (u32)g_reg.providers.count;
    mel_array_push(&g_reg.providers, e);
    return (Mel_Time_Format_Provider){ .index = idx, .generation = e.generation };
}

void mel_time_format_provider_unregister(Mel_Time_Format_Provider p)
{
    if (p.index < g_reg.providers.count && g_reg.providers.items[p.index].generation == p.generation)
        g_reg.providers.items[p.index].active = false;
}

void mel_time_format_init(const Mel_Alloc* alloc)
{
    if (g_reg.initialized)
        return;
    mel_assert(alloc != NULL);
    g_reg.alloc = alloc;
    mel_array_init(&g_reg.providers, g_reg.alloc);
    g_reg.provider_gen = 0;
    g_reg.cached = (Mel_Time_Format_Prefs){ 0 };
    g_reg.status = MEL_TIME_FMT_ERROR | MEL_TIME_FMT_UNAVAILABLE;
    g_reg.initialized = true;

    if (g_host_override != NULL)
        mel_time_format_provider_register(g_host_override);
    else
        mel_time_format__register_host_providers();
    mel_time_format_refresh();
}

void mel_time_format_shutdown(void)
{
    if (!g_reg.initialized)
        return;
    mel_array_free(&g_reg.providers);
    memset(&g_reg, 0, sizeof g_reg);
}

Mel_Time_Format_Status mel_time_format_refresh(void)
{
    mel_assert(g_reg.initialized);

    for (usize pi = 0; pi < g_reg.providers.count; pi++)
    {
        Provider_Entry* pe = &g_reg.providers.items[pi];
        if (!pe->active || !pe->desc.query)
            continue;

        Mel_Time_Format_Prefs raw = { 0 };
        if (!pe->desc.query(pe->desc.user, &raw))
            continue;

        Mel_Time_Format_Status st = MEL_TIME_FMT_OK;

        if (!order_is_singular(raw.date_order))
        {
            raw.date_order = MEL_DATE_ORDER_DMY;
            st |= MEL_TIME_FMT_WARNED | MEL_TIME_FMT_ORDER_GUESSED;
        }
        if (!clock_is_singular(raw.clock))
        {
            raw.clock = MEL_CLOCK_24H;
            st |= MEL_TIME_FMT_WARNED | MEL_TIME_FMT_CLOCK_GUESSED;
        }
        if (raw.date_separator[0] == '\0')
            raw.date_separator[0] = '/';
        raw.date_separator[3] = '\0';

        g_reg.cached = raw;
        g_reg.status = st;
        return st;
    }

    g_reg.cached = (Mel_Time_Format_Prefs){ 0 };
    g_reg.status = MEL_TIME_FMT_ERROR | MEL_TIME_FMT_UNAVAILABLE;
    return g_reg.status;
}

Mel_Time_Format_Result mel_time_format_prefs(void)
{
    if (!g_reg.initialized)
        return (Mel_Time_Format_Result){ .value = { 0 }, .status = MEL_TIME_FMT_ERROR | MEL_TIME_FMT_UNAVAILABLE };
    return (Mel_Time_Format_Result){ .value = g_reg.cached, .status = g_reg.status };
}

usize mel_time_format_date(Mel_Time_Format_Prefs p, i32 year, u32 month, u32 day, char* out, usize cap)
{
    mel_assert(order_is_singular(p.date_order));
    char sep = p.date_separator[0] ? p.date_separator[0] : '/';
    int  n;
    if (p.date_order == MEL_DATE_ORDER_YMD)
        n = snprintf(out, cap, "%04d%c%02u%c%02u", year, sep, month, sep, day);
    else if (p.date_order == MEL_DATE_ORDER_DMY)
        n = snprintf(out, cap, "%02u%c%02u%c%04d", day, sep, month, sep, year);
    else
        n = snprintf(out, cap, "%02u%c%02u%c%04d", month, sep, day, sep, year);
    return n < 0 ? 0 : (usize)n;
}
