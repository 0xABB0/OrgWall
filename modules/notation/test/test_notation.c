#include <test/test.h>

#include <math.h>
#include <notation/western.h>
#include <notation/identify.coro.h>
#include <allocator/heap.h>

static Mel_Tuning         g_tuning;
static Mel_NatAccNotation g_western;
static bool               g_ready;

static Mel_NatAccNotation* western(void)
{
    if (!g_ready)
    {
        g_tuning = mel_tuning_western(mel_alloc_heap(), mel_freq(440.0));
        g_western = mel_notation_western(mel_alloc_heap(), &g_tuning);
        g_ready = true;
    }
    return &g_western;
}

static bool symbol_is(str8 s, const char* expected)
{
    bool ok = str8_equals(s, str8_from_cstr(expected));
    if (s.data)
        mel_dealloc(mel_alloc_heap(), s.data);
    return ok;
}

MEL_TEST(notation, western_tuning_reference)
{
    Mel_NatAccNotation* nn = western();
    MEL_EXPECT(fabs(mel_freq_to_double(mel_tuning_frequency_for_index(nn->base.tuning, 9)) - 440.0) < 1e-6);
    MEL_EXPECT(fabs(mel_freq_to_double(mel_tuning_frequency_for_index(nn->base.tuning, 0)) - 261.625565) < 1e-5);
    MEL_EXPECT_EQ(mel_western_midi_to_index(69), 9);
    MEL_EXPECT_EQ(mel_western_index_to_midi(9), 69);
}

MEL_TEST(notation, note_parse)
{
    Mel_Note n;
    MEL_REQUIRE(mel_nat_acc_note(&n, western(), S8("C#"), 0));
    MEL_EXPECT_EQ(n.pitch.index, 1);
    MEL_EXPECT_EQ(n.nat_class, 0);
    MEL_EXPECT_EQ(n.acc_value, 1);

    MEL_REQUIRE(mel_nat_acc_note(&n, western(), S8("Bb"), 0));
    MEL_EXPECT_EQ(n.pitch.index, 10);

    MEL_REQUIRE(mel_nat_acc_note(&n, western(), S8("A"), 1));
    MEL_EXPECT_EQ(n.pitch.index, 21);

    MEL_EXPECT(!mel_nat_acc_note(&n, western(), S8("H"), 0));
    MEL_EXPECT(!mel_nat_acc_note(&n, western(), S8("Cq"), 0));
}

MEL_TEST(notation, note_symbol_roundtrip)
{
    Mel_Note n = mel_nat_acc_note_by_class(western(), 0, 0, 2);
    MEL_EXPECT(symbol_is(mel_nat_acc_note_symbol(western(), n, mel_alloc_heap()), "C##"));

    n = mel_nat_acc_note_by_class(western(), 6, 0, -1);
    MEL_EXPECT(symbol_is(mel_nat_acc_note_symbol(western(), n, mel_alloc_heap()), "Bb"));

    n = mel_nat_acc_note_by_class(western(), 4, -1, 0);
    MEL_EXPECT_EQ(n.pitch.index, -5);
    MEL_EXPECT(symbol_is(mel_nat_acc_note_symbol(western(), n, mel_alloc_heap()), "G"));
}

MEL_TEST(notation, guess_note_blueprint)
{
    Mel_Note n = mel_notation_guess_note(&western()->base, mel_pitch_make(western()->base.tuning, 13));
    MEL_EXPECT_EQ(n.nat_class, 0);
    MEL_EXPECT_EQ(n.nat_bi_index, 1);
    MEL_EXPECT_EQ(n.acc_value, 1);
    MEL_EXPECT_EQ(n.pitch.index, 13);
    MEL_EXPECT(symbol_is(mel_nat_acc_note_symbol(western(), n, mel_alloc_heap()), "C#"));

    n = mel_notation_guess_note(&western()->base, mel_pitch_make(western()->base.tuning, -2));
    MEL_EXPECT_EQ(n.pitch.index, -2);
    MEL_EXPECT(symbol_is(mel_nat_acc_note_symbol(western(), n, mel_alloc_heap()), "A#"));
}

static str8 interval_symbol_between(const char* a, i32 bi_a, const char* b, i32 bi_b)
{
    Mel_Note na, nb;
    MEL_REQUIRE(mel_nat_acc_note(&na, western(), str8_from_cstr(a), bi_a));
    MEL_REQUIRE(mel_nat_acc_note(&nb, western(), str8_from_cstr(b), bi_b));
    Mel_NoteInterval ni = mel_nat_acc_interval(western(), na, nb);
    return mel_nat_acc_interval_symbol(western(), ni, mel_alloc_heap());
}

MEL_TEST(notation, interval_names)
{
    MEL_EXPECT(symbol_is(interval_symbol_between("C", 0, "G", 0), "P5"));
    MEL_EXPECT(symbol_is(interval_symbol_between("C", 0, "Eb", 0), "m3"));
    MEL_EXPECT(symbol_is(interval_symbol_between("C", 0, "E", 0), "M3"));
    MEL_EXPECT(symbol_is(interval_symbol_between("F", 0, "B", 0), "A4"));
    MEL_EXPECT(symbol_is(interval_symbol_between("E", 0, "C", 1), "m6"));
    MEL_EXPECT(symbol_is(interval_symbol_between("C", 0, "C", 1), "P8"));
    MEL_EXPECT(symbol_is(interval_symbol_between("C", 0, "D", 1), "M9"));
    MEL_EXPECT(symbol_is(interval_symbol_between("G", 0, "C", 0), "-P5"));
    MEL_EXPECT(symbol_is(interval_symbol_between("C", 0, "C#", 0), "A1"));
}

MEL_TEST(notation, symbol_code_generate_failure_terminates)
{
    Mel_SymbolCode sc = mel_symbol_code_make(mel_alloc_heap());
    mel_symbol_code_add(&sc, S8("X"), 2);
    str8 out;
    MEL_EXPECT(!mel_symbol_code_generate(&sc, 3, mel_alloc_heap(), &out));
    MEL_EXPECT(mel_symbol_code_generate(&sc, 4, mel_alloc_heap(), &out));
    MEL_EXPECT(symbol_is(out, "XX"));
    mel_symbol_code_free(&sc);
}

MEL_TEST(notation, chord_identify)
{
    Mel_NatAccNotation* nn = western();
    Mel_Chord_Catalog   cat = mel_chord_catalog_western(mel_alloc_heap());

    Mel_Scale             c_major = mel_scale_from_indices(mel_alloc_heap(), nn->base.tuning, (i64[]){ 0, 4, 7 }, 3);
    Mel_Chord_Match_Array matches = mel_chord_identify(mel_alloc_heap(), &cat, &c_major, 0);
    MEL_REQUIRE_GE(matches.count, 1u);
    MEL_EXPECT_EQ(matches.items[0].root_pc, 0);
    MEL_EXPECT(str8_equals(cat.entries.items[matches.items[0].quality].name, S8("maj")));
    MEL_EXPECT_EQ(matches.items[0].bass_member, 0);
    mel_array_free(&matches);
    mel_scale_free(&c_major);

    Mel_Scale a_min_inv = mel_scale_from_indices(mel_alloc_heap(), nn->base.tuning, (i64[]){ 0, 4, 9 }, 3);
    matches = mel_chord_identify(mel_alloc_heap(), &cat, &a_min_inv, 0);
    MEL_REQUIRE_GE(matches.count, 1u);
    MEL_EXPECT_EQ(matches.items[0].root_pc, 9);
    MEL_EXPECT(str8_equals(cat.entries.items[matches.items[0].quality].name, S8("min")));
    MEL_EXPECT_EQ(matches.items[0].bass_member, 1);
    mel_array_free(&matches);
    mel_scale_free(&a_min_inv);

    Mel_Scale g7 = mel_scale_from_indices(mel_alloc_heap(), nn->base.tuning, (i64[]){ 2, 5, 7, 11 }, 4);
    matches = mel_chord_identify(mel_alloc_heap(), &cat, &g7, 7);
    MEL_REQUIRE_GE(matches.count, 1u);
    MEL_EXPECT_EQ(matches.items[0].root_pc, 7);
    MEL_EXPECT(str8_equals(cat.entries.items[matches.items[0].quality].name, S8("7")));
    MEL_EXPECT_EQ(matches.items[0].bass_member, 0);
    mel_array_free(&matches);
    mel_scale_free(&g7);

    mel_chord_catalog_free(&cat);
}

MEL_TEST(notation, western_patterns)
{
    Mel_NatAccNotation* nn = western();

    Mel_Pattern major = mel_western_pattern_major(mel_alloc_heap(), nn->base.tuning);
    MEL_EXPECT_EQ(mel_pattern_span(&major), 12);
    Mel_Scale c_major = mel_pattern_to_scale(mel_alloc_heap(), &major, mel_pitch_make(nn->base.tuning, 0));
    MEL_EXPECT(mel_scale_contains_index(&c_major, 4) && mel_scale_contains_index(&c_major, 11));
    MEL_EXPECT(!mel_scale_contains_index(&c_major, 6));

    Mel_Pattern aeolian = mel_western_pattern_mode(mel_alloc_heap(), nn->base.tuning, 5);
    Mel_Pattern minor = mel_western_pattern_natural_minor(mel_alloc_heap(), nn->base.tuning);
    MEL_EXPECT_EQ(mel_pattern_count(&aeolian), mel_pattern_count(&minor));
    for (i32 i = 0; i < mel_pattern_count(&minor); i++)
        MEL_EXPECT_EQ(mel_pattern_get(&aeolian, i), mel_pattern_get(&minor, i));

    mel_pattern_free(&major);
    mel_pattern_free(&aeolian);
    mel_pattern_free(&minor);
    mel_scale_free(&c_major);
}
