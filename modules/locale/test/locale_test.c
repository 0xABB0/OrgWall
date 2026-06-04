#include <test/test.h>
#include <locale/locale.h>
#include <locale/events.h>
#include <locale/provider.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <executor/executor.h>
#include <string/str8.h>

#include <string.h>

#include "../src/locale_backend.h"

static struct
{
    str8 tags[16];
    u32  count;
} g_fake;

static void fake_clear(void) { g_fake.count = 0; }
static void fake_add(const char* tag) { g_fake.tags[g_fake.count++] = str8_from_cstr(tag); }

static u32 fake_enumerate(void* user, const Mel_Alloc* alloc, Mel_Locale_Raw* out, u32 cap)
{
    (void)user;
    if (g_fake.count > cap)
        return g_fake.count;
    u32 produced = 0;
    for (u32 i = 0; i < g_fake.count; i++)
    {
        str8 t = g_fake.tags[i];
        u8*  buf = (u8*)mel_alloc(alloc, (usize)t.len);
        if (!buf)
            continue;
        memcpy(buf, t.data, (usize)t.len);
        out[produced++] = (Mel_Locale_Raw){ .tag = { .data = buf, .len = t.len } };
    }
    return produced;
}

static const Mel_Locale_Provider_Desc fake_desc = {
    .name = "test-fake",
    .enumerate = fake_enumerate,
};

static void install_fake(void)
{
    fake_clear();
    mel_locale__set_host_provider_override(&fake_desc);
}

MEL_TEST(locale, parses_language_and_country)
{
    install_fake();
    fake_add("en-US");
    mel_locale_init(mel_alloc_heap());

    MEL_REQUIRE_EQ(mel_locale_count(), 1u);
    Mel_Locale_Get_Result r = mel_locale_primary();
    MEL_EXPECT(mel_locale_status_ok(r.status));
    MEL_EXPECT_EQ_STR8(r.value.tag, S8("en-US"));
    MEL_EXPECT_EQ_STR8(r.value.language, S8("en"));
    MEL_EXPECT_EQ_STR8(r.value.country, S8("US"));
    MEL_EXPECT(mel_locale_has_country(r.value));

    mel_locale_shutdown();
}

MEL_TEST(locale, normalizes_case_and_underscore_separator)
{
    install_fake();
    fake_add("EN_us");
    mel_locale_init(mel_alloc_heap());

    Mel_Locale_Get_Result r = mel_locale_at(0);
    MEL_EXPECT_EQ_STR8(r.value.tag, S8("en-US"));
    MEL_EXPECT_EQ_STR8(r.value.language, S8("en"));
    MEL_EXPECT_EQ_STR8(r.value.country, S8("US"));

    mel_locale_shutdown();
}

MEL_TEST(locale, generic_language_has_no_country)
{
    install_fake();
    fake_add("de");
    mel_locale_init(mel_alloc_heap());

    Mel_Locale_Get_Result r = mel_locale_at(0);
    MEL_EXPECT(mel_locale_status_no_country(r.status));
    MEL_EXPECT(!mel_locale_has_country(r.value));
    MEL_EXPECT_EQ_STR8(r.value.language, S8("de"));
    MEL_EXPECT_EQ(r.value.country.len, (size)0);

    mel_locale_shutdown();
}

MEL_TEST(locale, preserves_preference_order)
{
    install_fake();
    fake_add("fr-FR");
    fake_add("en-GB");
    fake_add("ja");
    mel_locale_init(mel_alloc_heap());

    MEL_REQUIRE_EQ(mel_locale_count(), 3u);
    Mel_Locale got[8];
    u32        n = mel_locale_list(got, 8);
    MEL_REQUIRE_EQ(n, 3u);
    MEL_EXPECT_EQ_STR8(got[0].tag, S8("fr-FR"));
    MEL_EXPECT_EQ_STR8(got[1].tag, S8("en-GB"));
    MEL_EXPECT_EQ_STR8(got[2].tag, S8("ja"));

    mel_locale_shutdown();
}

MEL_TEST(locale, empty_primary_is_loud_error)
{
    install_fake();
    mel_locale_init(mel_alloc_heap());

    MEL_EXPECT_EQ(mel_locale_count(), 0u);
    Mel_Locale_Get_Result r = mel_locale_primary();
    MEL_EXPECT(mel_locale_status_failed(r.status));
    MEL_EXPECT(mel_locale_status_empty(r.status));

    mel_locale_shutdown();
}

MEL_TEST(locale, out_of_range_is_loud_error)
{
    install_fake();
    fake_add("it-IT");
    mel_locale_init(mel_alloc_heap());

    Mel_Locale_Get_Result r = mel_locale_at(5);
    MEL_EXPECT(mel_locale_status_failed(r.status));
    MEL_EXPECT((r.status & MEL_LOCALE_OUT_OF_RANGE) != 0u);

    mel_locale_shutdown();
}

MEL_TEST(locale, equal_compares_full_tag)
{
    install_fake();
    fake_add("pt-BR");
    fake_add("pt-PT");
    mel_locale_init(mel_alloc_heap());

    Mel_Locale a = mel_locale_at(0).value;
    Mel_Locale b = mel_locale_at(1).value;
    MEL_EXPECT(!mel_locale_equal(a, b));
    MEL_EXPECT(mel_locale_equal(a, a));

    mel_locale_shutdown();
}

static u32 drain_changed(void)
{
    Mel_Locale_Event ev[16];
    u32              got = mel_locale_poll_events(ev, 16);
    u32              acc = 0;
    for (u32 i = 0; i < got; i++)
        acc |= ev[i].changed_fields;
    return acc;
}

MEL_TEST(locale, refresh_emits_diffed_change_event)
{
    install_fake();
    fake_add("en-US");
    mel_locale_init(mel_alloc_heap());

    Mel_Locale_Event drain[16];
    mel_locale_poll_events(drain, 16);

    fake_clear();
    fake_add("fr-FR");
    fake_add("en-US");
    mel_locale_refresh();

    u32 changed = drain_changed();
    MEL_EXPECT((changed & MEL_LOCALE_FIELD_PRIMARY) != 0u);
    MEL_EXPECT((changed & MEL_LOCALE_FIELD_MEMBERSHIP) != 0u);
    MEL_EXPECT_EQ(mel_locale_count(), 2u);

    mel_locale_poll_events(drain, 16);
    mel_locale_refresh();
    MEL_EXPECT_EQ(drain_changed(), 0u);

    mel_locale_shutdown();
}

MEL_TEST(locale, reorder_only_is_order_field)
{
    install_fake();
    fake_add("en-US");
    fake_add("fr-FR");
    mel_locale_init(mel_alloc_heap());

    Mel_Locale_Event drain[16];
    mel_locale_poll_events(drain, 16);

    fake_clear();
    fake_add("fr-FR");
    fake_add("en-US");
    mel_locale_refresh();

    u32 changed = drain_changed();
    MEL_EXPECT((changed & MEL_LOCALE_FIELD_ORDER) != 0u);
    MEL_EXPECT((changed & MEL_LOCALE_FIELD_PRIMARY) != 0u);
    MEL_EXPECT((changed & MEL_LOCALE_FIELD_MEMBERSHIP) == 0u);

    mel_locale_shutdown();
}

typedef struct
{
    u32 fires;
    u32 changed;
} Push_Sink;

static void push_cb(const Mel_Locale_Event* ev, void* user)
{
    Push_Sink* s = (Push_Sink*)user;
    s->fires++;
    s->changed |= ev->changed_fields;
}

MEL_TEST(locale, subscribe_delivers_on_refresh)
{
    install_fake();
    fake_add("en-US");
    mel_locale_init_ex(mel_alloc_heap(), mel_executor_inline());

    Mel_Locale_Event drain[16];
    mel_locale_poll_events(drain, 16);

    Push_Sink               s = { 0 };
    Mel_Locale_Subscription sub = mel_locale_subscribe(mel_executor_inline(), push_cb, &s);
    MEL_REQUIRE(mel_slotmap_handle_valid(sub.handle));

    fake_clear();
    fake_add("es-ES");
    mel_locale_refresh();

    MEL_EXPECT_EQ(s.fires, 1u);
    MEL_EXPECT((s.changed & MEL_LOCALE_FIELD_PRIMARY) != 0u);

    mel_locale_unsubscribe(sub);
    mel_locale_shutdown();
}

MEL_TEST(locale, subscribe_without_executor_is_loud_null)
{
    install_fake();
    mel_locale_init(mel_alloc_heap());

    Push_Sink               s = { 0 };
    Mel_Locale_Subscription sub = mel_locale_subscribe(NULL, push_cb, &s);
    MEL_EXPECT(!mel_slotmap_handle_valid(sub.handle));

    mel_locale_shutdown();
}
