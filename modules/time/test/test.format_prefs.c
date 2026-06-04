#include <test/test.h>
#include <time/format_prefs.h>
#include <time/format_provider.h>

#include <allocator/heap.h>

#include <string.h>

#include "../src/format_backend.h"

static Mel_Time_Format_Prefs g_fake;
static bool                   g_fake_ok;

static bool fake_query(void* user, Mel_Time_Format_Prefs* out)
{
    (void)user;
    if (!g_fake_ok)
        return false;
    *out = g_fake;
    return true;
}

static const Mel_Time_Format_Provider_Desc fake_desc = {
    .name = "fake",
    .query = fake_query,
};

static void set_fake(u32 order, u32 clock, const char* sep)
{
    memset(&g_fake, 0, sizeof g_fake);
    g_fake.date_order = order;
    g_fake.clock = clock;
    if (sep)
        strncpy(g_fake.date_separator, sep, 3);
    g_fake_ok = true;
}

MEL_TEST(format_prefs, dmy_24h_resolved)
{
    set_fake(MEL_DATE_ORDER_DMY, MEL_CLOCK_24H, "/");
    mel_time_format__set_host_provider_override(&fake_desc);
    mel_time_format_init(mel_alloc_heap());

    Mel_Time_Format_Result r = mel_time_format_prefs();
    MEL_EXPECT(mel_time_fmt_status_ok(r.status));
    MEL_EXPECT_EQ(r.value.date_order, (u32)MEL_DATE_ORDER_DMY);
    MEL_EXPECT_EQ(r.value.clock, (u32)MEL_CLOCK_24H);

    char buf[32];
    usize n = mel_time_format_date(r.value, 2024, 6, 1, buf, sizeof buf);
    MEL_EXPECT_EQ(n, 10u);
    MEL_EXPECT_STR_EQ(buf, "01/06/2024");

    mel_time_format_shutdown();
}

MEL_TEST(format_prefs, ymd_with_dash)
{
    set_fake(MEL_DATE_ORDER_YMD, MEL_CLOCK_24H, "-");
    mel_time_format__set_host_provider_override(&fake_desc);
    mel_time_format_init(mel_alloc_heap());

    Mel_Time_Format_Result r = mel_time_format_prefs();
    char                   buf[32];
    mel_time_format_date(r.value, 2024, 6, 1, buf, sizeof buf);
    MEL_EXPECT_STR_EQ(buf, "2024-06-01");

    mel_time_format_shutdown();
}

MEL_TEST(format_prefs, mdy_12h_resolved)
{
    set_fake(MEL_DATE_ORDER_MDY, MEL_CLOCK_12H, "/");
    mel_time_format__set_host_provider_override(&fake_desc);
    mel_time_format_init(mel_alloc_heap());

    Mel_Time_Format_Result r = mel_time_format_prefs();
    MEL_EXPECT_EQ(r.value.clock, (u32)MEL_CLOCK_12H);
    char buf[32];
    mel_time_format_date(r.value, 2024, 6, 1, buf, sizeof buf);
    MEL_EXPECT_STR_EQ(buf, "06/01/2024");

    mel_time_format_shutdown();
}

MEL_TEST(format_prefs, non_singular_order_warns_and_guesses)
{
    set_fake(MEL_DATE_ORDER_DMY | MEL_DATE_ORDER_MDY, MEL_CLOCK_24H, "/");
    mel_time_format__set_host_provider_override(&fake_desc);
    mel_time_format_init(mel_alloc_heap());

    Mel_Time_Format_Result r = mel_time_format_prefs();
    MEL_EXPECT(mel_time_fmt_status_warned(r.status));
    MEL_EXPECT((r.status & MEL_TIME_FMT_ORDER_GUESSED) != 0u);
    MEL_EXPECT_EQ(r.value.date_order, (u32)MEL_DATE_ORDER_DMY);

    mel_time_format_shutdown();
}

MEL_TEST(format_prefs, provider_declines_is_unavailable)
{
    g_fake_ok = false;
    mel_time_format__set_host_provider_override(&fake_desc);
    mel_time_format_init(mel_alloc_heap());

    Mel_Time_Format_Result r = mel_time_format_prefs();
    MEL_EXPECT(mel_time_fmt_status_failed(r.status));
    MEL_EXPECT(mel_time_fmt_status_unavailable(r.status));

    mel_time_format_shutdown();
}

MEL_TEST(format_prefs, missing_separator_defaults_to_slash)
{
    set_fake(MEL_DATE_ORDER_DMY, MEL_CLOCK_24H, NULL);
    mel_time_format__set_host_provider_override(&fake_desc);
    mel_time_format_init(mel_alloc_heap());

    Mel_Time_Format_Result r = mel_time_format_prefs();
    char                   buf[32];
    mel_time_format_date(r.value, 2024, 6, 1, buf, sizeof buf);
    MEL_EXPECT_STR_EQ(buf, "01/06/2024");

    mel_time_format_shutdown();
}
