#include <notation/western.h>

#include <assert.h>
#include <math/real.h>

Mel_Tuning mel_tuning_western(const Mel_Alloc* alloc, Mel_Hz a4)
{
    MEL_REAL_PROTECT_FLAGS;
    mpfr_t    semitones;
    mp_limb_t semitones_limbs[MEL_REAL_LIMBS];
    mel_real_scratch(semitones, semitones_limbs);
    mpfr_set_si(semitones, -9, MPFR_RNDN);
    Mel_Hz c4 = mel_freq_transpose_semitones(a4, semitones);
    return mel_tuning_edo(alloc, 12, c4);
}

Mel_NatAccNotation mel_notation_western(const Mel_Alloc* alloc, const Mel_Tuning* tuning)
{
    assert(tuning->period == 12);

    Mel_NatAccNotation nn = mel_nat_acc_notation_make(alloc, tuning);

    static const char* naturals[] = { "C", "D", "E", "F", "G", "A", "B" };
    static const i32   nat_pitches[] = { 0, 2, 4, 5, 7, 9, 11 };
    for (i32 i = 0; i < 7; i++)
        mel_nat_acc_add_natural(&nn, str8_from_cstr(naturals[i]), nat_pitches[i]);

    mel_nat_acc_add_accidental(&nn, S8("#"), 1);
    mel_nat_acc_add_accidental(&nn, S8("b"), -1);

    static const i32 perfect_classes[] = { 0, 3, 4 };
    static const i32 perfect_refs[] = { 0, 5, 7 };
    static const i32 major_classes[] = { 1, 2, 5, 6 };
    static const i32 major_refs[] = { 2, 4, 9, 11 };

    for (i32 i = 0; i < 3; i++)
    {
        Mel_SymbolCode code = mel_symbol_code_make(alloc);
        mel_symbol_code_add(&code, S8("P"), 0);
        mel_symbol_code_add(&code, S8("A"), 1);
        mel_symbol_code_add(&code, S8("d"), -1);
        mel_nat_acc_set_interval_code(&nn, perfect_classes[i], code, perfect_refs[i]);
    }

    for (i32 i = 0; i < 4; i++)
    {
        Mel_SymbolCode code = mel_symbol_code_make(alloc);
        mel_symbol_code_add(&code, S8("M"), 0);
        mel_symbol_code_add(&code, S8("d"), -2);
        mel_symbol_code_add(&code, S8("m"), -1);
        mel_symbol_code_add(&code, S8("A"), 1);
        mel_nat_acc_set_interval_code(&nn, major_classes[i], code, major_refs[i]);
    }

    static const i32 blueprint_class[] = { 0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6 };
    static const i32 blueprint_acc[] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };

    Mel_Note blueprint[12];
    for (i32 pc = 0; pc < 12; pc++)
        blueprint[pc] = mel_nat_acc_note_by_class(&nn, blueprint_class[pc], 0, blueprint_acc[pc]);

    mel_notation_set_enharmonic(&nn.base, mel_enharmonic_pc_blueprint(alloc, blueprint, 12));

    return nn;
}

Mel_Note mel_western_note(const Mel_NatAccNotation* nn, i32 nat_class, i32 octave, i32 acc_value) { return mel_nat_acc_note_by_class(nn, nat_class, octave - 4, acc_value); }

static Mel_Pattern mel_western__pattern(const Mel_Alloc* alloc, const Mel_Tuning* tuning, const i64* diffs, i32 count)
{
    assert(tuning->period == 12);
    return mel_pattern_from_diffs(alloc, tuning, diffs, count);
}

Mel_Pattern mel_western_pattern_major(const Mel_Alloc* alloc, const Mel_Tuning* tuning)
{
    static const i64 diffs[] = { 2, 2, 1, 2, 2, 2, 1 };
    return mel_western__pattern(alloc, tuning, diffs, 7);
}

Mel_Pattern mel_western_pattern_natural_minor(const Mel_Alloc* alloc, const Mel_Tuning* tuning)
{
    static const i64 diffs[] = { 2, 1, 2, 2, 1, 2, 2 };
    return mel_western__pattern(alloc, tuning, diffs, 7);
}

Mel_Pattern mel_western_pattern_harmonic_minor(const Mel_Alloc* alloc, const Mel_Tuning* tuning)
{
    static const i64 diffs[] = { 2, 1, 2, 2, 1, 3, 1 };
    return mel_western__pattern(alloc, tuning, diffs, 7);
}

Mel_Pattern mel_western_pattern_melodic_minor(const Mel_Alloc* alloc, const Mel_Tuning* tuning)
{
    static const i64 diffs[] = { 2, 1, 2, 2, 2, 2, 1 };
    return mel_western__pattern(alloc, tuning, diffs, 7);
}

Mel_Pattern mel_western_pattern_mode(const Mel_Alloc* alloc, const Mel_Tuning* tuning, i32 degree)
{
    assert(degree >= 0 && degree < 7);
    static const i64 major[] = { 2, 2, 1, 2, 2, 2, 1 };
    i64              diffs[7];
    for (i32 i = 0; i < 7; i++)
        diffs[i] = major[(degree + i) % 7];
    return mel_western__pattern(alloc, tuning, diffs, 7);
}

Mel_Pattern mel_western_pattern_major_pentatonic(const Mel_Alloc* alloc, const Mel_Tuning* tuning)
{
    static const i64 diffs[] = { 2, 2, 3, 2, 3 };
    return mel_western__pattern(alloc, tuning, diffs, 5);
}

Mel_Pattern mel_western_pattern_minor_pentatonic(const Mel_Alloc* alloc, const Mel_Tuning* tuning)
{
    static const i64 diffs[] = { 3, 2, 2, 3, 2 };
    return mel_western__pattern(alloc, tuning, diffs, 5);
}

Mel_Pattern mel_western_pattern_blues(const Mel_Alloc* alloc, const Mel_Tuning* tuning)
{
    static const i64 diffs[] = { 3, 2, 1, 1, 3, 2 };
    return mel_western__pattern(alloc, tuning, diffs, 6);
}

Mel_Pattern mel_western_pattern_chromatic(const Mel_Alloc* alloc, const Mel_Tuning* tuning)
{
    static const i64 diffs[] = { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
    return mel_western__pattern(alloc, tuning, diffs, 12);
}

Mel_Chord_Catalog mel_chord_catalog_western(const Mel_Alloc* alloc)
{
    Mel_Chord_Catalog cat = mel_chord_catalog_make(alloc);

    static const struct
    {
        const char* name;
        i64         diffs[4];
        i32         count;
    } qualities[] = {
        { "maj", { 4, 7 }, 2 },       { "min", { 3, 7 }, 2 },      { "dim", { 3, 6 }, 2 },         { "aug", { 4, 8 }, 2 },         { "sus2", { 2, 7 }, 2 },        { "sus4", { 5, 7 }, 2 }, { "maj7", { 4, 7, 11 }, 3 },
        { "7", { 4, 7, 10 }, 3 },     { "min7", { 3, 7, 10 }, 3 }, { "minMaj7", { 3, 7, 11 }, 3 }, { "dim7", { 3, 6, 9 }, 3 },     { "min7b5", { 3, 6, 10 }, 3 },  { "6", { 4, 7, 9 }, 3 }, { "min6", { 3, 7, 9 }, 3 },
        { "7sus4", { 5, 7, 10 }, 3 }, { "add9", { 2, 4, 7 }, 3 },  { "9", { 2, 4, 7, 10 }, 4 },    { "maj9", { 2, 4, 7, 11 }, 4 }, { "min9", { 2, 3, 7, 10 }, 4 },
    };

    for (usize i = 0; i < sizeof(qualities) / sizeof(qualities[0]); i++)
        mel_chord_catalog_add(&cat, str8_from_cstr(qualities[i].name), qualities[i].diffs, qualities[i].count);

    return cat;
}
