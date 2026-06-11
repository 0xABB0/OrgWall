#include <test/test.h>

#include <math.h>
#include <musictuning/tuning.h>
#include <musictuning/scala.h>
#include <allocator/heap.h>

static bool freq_near(Mel_Hz f, double expected) { return fabs(mel_freq_to_double(f) - expected) < 1e-6; }

MEL_TEST(tuning, edo_frequencies)
{
    Mel_Tuning t = mel_tuning_edo(mel_alloc_heap(), 12, mel_freq(440.0));
    MEL_EXPECT(freq_near(mel_tuning_frequency_for_index(&t, 0), 440.0));
    MEL_EXPECT(freq_near(mel_tuning_frequency_for_index(&t, 12), 880.0));
    MEL_EXPECT(freq_near(mel_tuning_frequency_for_index(&t, -12), 220.0));
    MEL_EXPECT(freq_near(mel_tuning_frequency_for_index(&t, 7), 440.0 * pow(2.0, 7.0 / 12.0)));
    mel_tuning_free(&t);
}

MEL_TEST(tuning, ed_tritave)
{
    Mel_Tuning t = mel_tuning_ed(mel_alloc_heap(), 13, 3, 1, mel_freq(440.0));
    MEL_EXPECT_EQ(t.period, 13u);
    MEL_EXPECT(freq_near(mel_tuning_frequency_for_index(&t, 13), 1320.0));
    mel_tuning_free(&t);
}

MEL_TEST(tuning, find_index_roundtrip)
{
    Mel_Tuning t = mel_tuning_edo(mel_alloc_heap(), 12, mel_freq(440.0));
    for (i64 i = -30; i <= 30; i++)
        MEL_EXPECT_EQ(mel_tuning_find_index(&t, mel_tuning_frequency_for_index(&t, i)), i);
    mel_tuning_free(&t);
}

MEL_TEST(tuning, custom_steps)
{
    Mel_Tuning t = mel_tuning_custom(mel_alloc_heap(), 2, mel_freq(440.0));
    mel_tuning_custom_set_step_rational(&t, 1, 3, 2);
    MEL_EXPECT(freq_near(mel_tuning_frequency_for_index(&t, 0), 440.0));
    MEL_EXPECT(freq_near(mel_tuning_frequency_for_index(&t, 1), 660.0));
    MEL_EXPECT(freq_near(mel_tuning_frequency_for_index(&t, 2), 880.0));
    MEL_EXPECT(freq_near(mel_tuning_frequency_for_index(&t, 3), 1320.0));
    MEL_EXPECT(freq_near(mel_tuning_frequency_for_index(&t, -1), 330.0));
    mel_tuning_free(&t);
}

MEL_TEST(tuning, scala_parse)
{
    Mel_Tuning t;
    MEL_REQUIRE(mel_scala_parse(&t, mel_alloc_heap(), S8("Test scale\n2\n700.0\n2/1\n"), mel_freq(440.0)));
    MEL_EXPECT_EQ(t.period, 2u);
    MEL_EXPECT(freq_near(mel_tuning_frequency_for_index(&t, 1), 440.0 * pow(2.0, 700.0 / 1200.0)));
    MEL_EXPECT(freq_near(mel_tuning_frequency_for_index(&t, 2), 880.0));
    mel_tuning_free(&t);
}

MEL_TEST(tuning, scala_parse_with_comments_and_description)
{
    Mel_Tuning t;
    MEL_REQUIRE(mel_scala_parse(&t, mel_alloc_heap(), S8("! file.scl\n! comment\nMy 12-tone description\n2\n! values\n700.0\n2/1\n"), mel_freq(440.0)));
    MEL_EXPECT_EQ(t.period, 2u);
    mel_tuning_free(&t);
}

MEL_TEST(tuning, scala_parse_failures)
{
    Mel_Tuning t;
    MEL_EXPECT(!mel_scala_parse(&t, mel_alloc_heap(), S8("desc\n0\n"), mel_freq(440.0)));
    MEL_EXPECT(!mel_scala_parse(&t, mel_alloc_heap(), S8("desc\n2\n700.0\n"), mel_freq(440.0)));
    MEL_EXPECT(!mel_scala_parse(&t, mel_alloc_heap(), S8("desc\n2\nnonsense\n2/1\n"), mel_freq(440.0)));
    MEL_EXPECT(!mel_scala_parse(&t, mel_alloc_heap(), S8(""), mel_freq(440.0)));
}

MEL_TEST(tuning, scala_roundtrip)
{
    Mel_Tuning t = mel_tuning_edo(mel_alloc_heap(), 12, mel_freq(440.0));
    str8       exported = mel_scala_export(&t, S8("roundtrip"), mel_alloc_heap());
    MEL_REQUIRE(exported.len > 0);

    Mel_Tuning back;
    MEL_REQUIRE(mel_scala_parse(&back, mel_alloc_heap(), exported, mel_freq(440.0)));
    MEL_EXPECT_EQ(back.period, 12u);
    for (i64 i = 0; i <= 12; i++)
        MEL_EXPECT(fabs(mel_freq_to_double(mel_tuning_frequency_for_index(&back, i)) - mel_freq_to_double(mel_tuning_frequency_for_index(&t, i))) < 1e-3);

    mel_dealloc(mel_alloc_heap(), exported.data);
    mel_tuning_free(&back);
    mel_tuning_free(&t);
}

MEL_TEST(tuning, generators)
{
    Mel_Tuning t = mel_tuning_edo(mel_alloc_heap(), 12, mel_freq(440.0));
    i64        gens[12];
    i32        count = mel_tuning_get_generators(&t, gens, 12);
    MEL_EXPECT_EQ(count, 4);
    MEL_EXPECT_EQ(gens[0], 1);
    MEL_EXPECT_EQ(gens[1], 5);
    MEL_EXPECT_EQ(gens[2], 7);
    MEL_EXPECT_EQ(gens[3], 11);
    mel_tuning_free(&t);
}
