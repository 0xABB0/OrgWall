#include <time/clock.h>
#include <test/test.h>

#include <allocator/heap.h>
#include <string/str8.h>

MEL_TEST(clock, epoch_is_thursday)
{
    Mel_Civil c = mel_civil_from_unix_ns(0, 0);
    MEL_EXPECT_EQ(c.year, 1970);
    MEL_EXPECT_EQ(c.month, 1u);
    MEL_EXPECT_EQ(c.day, 1u);
    MEL_EXPECT_EQ(c.hour, 0u);
    MEL_EXPECT_EQ(c.weekday, 4u);
}

MEL_TEST(clock, known_instant)
{
    mel_nanosec t = (mel_nanosec)1717200000 * MEL_NANOS_PER_SEC;
    Mel_Civil   c = mel_civil_from_unix_ns(t, 0);
    MEL_EXPECT_EQ(c.year, 2024);
    MEL_EXPECT_EQ(c.month, 6u);
    MEL_EXPECT_EQ(c.day, 1u);
    MEL_EXPECT_EQ(c.hour, 0u);
    MEL_EXPECT_EQ(c.minute, 0u);
    MEL_EXPECT_EQ(c.second, 0u);
    MEL_EXPECT_EQ(c.weekday, 6u);
}

MEL_TEST(clock, roundtrip_civil_unix)
{
    for (i64 base = -62135596800; base <= 4102444800; base += 98765431)
    {
        mel_nanosec t = (mel_nanosec)((i64)base * MEL_NANOS_PER_SEC);
        Mel_Civil   c = mel_civil_from_unix_ns(t, 0);
        MEL_EXPECT_EQ(mel_civil_to_unix_ns(c), t);
    }
}

MEL_TEST(clock, timezone_offset_applied)
{
    mel_nanosec t = (mel_nanosec)1717200000 * MEL_NANOS_PER_SEC;
    Mel_Civil   c = mel_civil_from_unix_ns(t, 120);
    MEL_EXPECT_EQ(c.hour, 2u);
    MEL_EXPECT_EQ(c.tz_offset_min, 120);
    MEL_EXPECT_EQ(mel_civil_to_unix_ns(c), t);
}

MEL_TEST(clock, pre_epoch)
{
    Mel_Civil c = mel_civil_from_unix_ns((mel_nanosec)((i64)(-1) * MEL_NANOS_PER_SEC), 0);
    MEL_EXPECT_EQ(c.year, 1969);
    MEL_EXPECT_EQ(c.month, 12u);
    MEL_EXPECT_EQ(c.day, 31u);
    MEL_EXPECT_EQ(c.hour, 23u);
    MEL_EXPECT_EQ(c.minute, 59u);
    MEL_EXPECT_EQ(c.second, 59u);
}

MEL_TEST(clock, iso8601_utc)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Civil        c = mel_civil_from_unix_ns((mel_nanosec)1717200000 * MEL_NANOS_PER_SEC, 0);
    str8             s = mel_civil_iso8601(a, c);
    MEL_EXPECT_EQ_STR8(s, S8("2024-06-01T00:00:00Z"));
    mel_dealloc(a, s.data);
}

MEL_TEST(clock, iso8601_offset_and_frac)
{
    const Mel_Alloc* a = mel_alloc_heap();
    Mel_Civil        c = { .year = 2024, .month = 6, .day = 1, .hour = 2, .minute = 30, .second = 15, .nanosecond = 123000000, .tz_offset_min = 120 };
    str8             s = mel_civil_iso8601(a, c);
    MEL_EXPECT_EQ_STR8(s, S8("2024-06-01T02:30:15.123000000+02:00"));
    mel_dealloc(a, s.data);
}

MEL_TEST(clock, parse_roundtrip)
{
    const Mel_Alloc* a = mel_alloc_heap();
    str8             in = S8("2024-06-01T02:30:15.123000000+02:00");
    Mel_Civil        c;
    MEL_EXPECT(mel_civil_parse_iso8601(in, &c));
    MEL_EXPECT_EQ(c.year, 2024);
    MEL_EXPECT_EQ(c.month, 6u);
    MEL_EXPECT_EQ(c.day, 1u);
    MEL_EXPECT_EQ(c.hour, 2u);
    MEL_EXPECT_EQ(c.minute, 30u);
    MEL_EXPECT_EQ(c.second, 15u);
    MEL_EXPECT_EQ(c.nanosecond, 123000000u);
    MEL_EXPECT_EQ(c.tz_offset_min, 120);

    str8 s = mel_civil_iso8601(a, c);
    MEL_EXPECT_EQ_STR8(s, in);
    mel_dealloc(a, s.data);
}

MEL_TEST(clock, parse_z_and_no_frac)
{
    Mel_Civil c;
    MEL_EXPECT(mel_civil_parse_iso8601(S8("1970-01-01T00:00:00Z"), &c));
    MEL_EXPECT_EQ(mel_civil_to_unix_ns(c), 0u);
}

MEL_TEST(clock, parse_rejects_malformed)
{
    Mel_Civil c;
    MEL_EXPECT(!mel_civil_parse_iso8601(S8("2024-13-01T00:00:00Z"), &c));
    MEL_EXPECT(!mel_civil_parse_iso8601(S8("2024-06-01 00:00:00"), &c));
    MEL_EXPECT(!mel_civil_parse_iso8601(S8("2024-06-01T00:00:60Z"), &c));
    MEL_EXPECT(!mel_civil_parse_iso8601(S8("garbage"), &c));
}

MEL_TEST(clock, anchor_maps_mono_to_wall)
{
    Mel_Clock_Anchor a = { .mono = 1000, .wall = (mel_nanosec)5000 };
    MEL_EXPECT_EQ(mel_wall_from_mono(&a, 1000 + mel_dur_ms(7)), (mel_nanosec)(5000 + mel_dur_ms(7)));
    MEL_EXPECT_EQ(mel_wall_from_mono(&a, 1000), (mel_nanosec)5000);
}
