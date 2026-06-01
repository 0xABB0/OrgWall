#include <time/duration.h>
#include <test/test.h>

#include <allocator/heap.h>
#include <string/str8.h>

MEL_TEST(duration, constructors_scale)
{
    MEL_EXPECT_EQ(mel_dur_us(1), MEL_NANOS_PER_US);
    MEL_EXPECT_EQ(mel_dur_ms(1), MEL_NANOS_PER_MS);
    MEL_EXPECT_EQ(mel_dur_secs(1), MEL_NANOS_PER_SEC);
    MEL_EXPECT_EQ(mel_dur_mins(1), MEL_NANOS_PER_MIN);
    MEL_EXPECT_EQ(mel_dur_secs(2), 2 * MEL_NANOS_PER_SEC);
}

MEL_TEST(duration, extractors_truncate)
{
    Mel_Duration d = mel_dur_ms(1500);
    MEL_EXPECT_EQ(mel_dur_to_ms(d), 1500);
    MEL_EXPECT_EQ(mel_dur_to_us(d), 1500000);
    MEL_EXPECT_FLOAT_EQ(mel_dur_as_secs_f64(d), 1.5, 1e-9);
    MEL_EXPECT_FLOAT_EQ(mel_dur_as_ms_f64(d), 1500.0, 1e-9);
}

MEL_TEST(duration, f64_constructor_clamps_inf)
{
    f64 inf = (f64)MEL_DURATION_MAX * 1e9;
    MEL_EXPECT_EQ(mel_dur_secs_f64(inf), MEL_DURATION_MAX);
    MEL_EXPECT_EQ(mel_dur_secs_f64(-inf), MEL_DURATION_MIN);
}

MEL_TEST(duration, arithmetic_saturates)
{
    MEL_EXPECT_EQ(mel_dur_secs(INT64_MAX), MEL_DURATION_MAX);
    MEL_EXPECT_EQ(mel_dur_secs(INT64_MIN), MEL_DURATION_MIN);
    MEL_EXPECT_EQ(mel_dur_add(MEL_DURATION_MAX, 1), MEL_DURATION_MAX);
    MEL_EXPECT_EQ(mel_dur_sub(MEL_DURATION_MIN, 1), MEL_DURATION_MIN);
    MEL_EXPECT_EQ(mel_dur_scale(MEL_DURATION_MAX, 2), MEL_DURATION_MAX);
    MEL_EXPECT_EQ(mel_dur_scale(MEL_DURATION_MAX, -2), MEL_DURATION_MIN);
}

MEL_TEST(duration, abs_of_min_saturates)
{
    MEL_EXPECT_EQ(mel_dur_abs(MEL_DURATION_MIN), MEL_DURATION_MAX);
    MEL_EXPECT_EQ(mel_dur_abs(mel_dur_ms(-5)), mel_dur_ms(5));
}

MEL_TEST(duration, compare)
{
    MEL_EXPECT_EQ(mel_dur_cmp(mel_dur_ms(1), mel_dur_ms(2)), -1);
    MEL_EXPECT_EQ(mel_dur_cmp(mel_dur_ms(2), mel_dur_ms(1)), 1);
    MEL_EXPECT_EQ(mel_dur_cmp(mel_dur_ms(2), mel_dur_ms(2)), 0);
    MEL_EXPECT_EQ(mel_dur_min(mel_dur_ms(1), mel_dur_ms(2)), mel_dur_ms(1));
    MEL_EXPECT_EQ(mel_dur_max(mel_dur_ms(1), mel_dur_ms(2)), mel_dur_ms(2));
}

static void expect_dur_str(const Mel_Alloc* a, Mel_Duration d, str8 want)
{
    str8 got = mel_dur_str(a, d);
    MEL_EXPECT_EQ_STR8(got, want);
    mel_dealloc(a, got.data);
}

MEL_TEST(duration, str_units)
{
    const Mel_Alloc* a = mel_alloc_heap();
    expect_dur_str(a, mel_dur_ns(42), S8("42ns"));
    expect_dur_str(a, mel_dur_ms(1) + mel_dur_us(250), S8("1.250ms"));
    expect_dur_str(a, mel_dur_secs(3) + mel_dur_ms(500), S8("3.500s"));
    expect_dur_str(a, mel_dur_mins(3) + mel_dur_secs(4) + mel_dur_ms(500), S8("3m04.500s"));
    expect_dur_str(a, mel_dur_mins(125), S8("2h05m00s"));
    expect_dur_str(a, mel_dur_ms(-1) - mel_dur_us(250), S8("-1.250ms"));
}

MEL_TEST(duration, format_needed_length_with_zero_cap)
{
    usize n = mel_dur_format(mel_dur_ns(42), NULL, 0);
    MEL_EXPECT_EQ(n, 4);
}
