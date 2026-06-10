#include <test/test.h>

#include <math.h>
#include <musictheory/pitch.h>
#include <musictheory/interval.h>
#include <musictheory/scale.h>
#include <musictheory/pattern.h>
#include <musictheory/chord.h>
#include <musictheory/scale_gen.coro.h>
#include <frequency/cent.h>
#include <allocator/heap.h>

static Mel_Tuning t12;

static const Mel_Tuning* edo12(void)
{
    if (!t12.ctx)
        t12 = mel_tuning_edo(mel_alloc_heap(), 12, mel_freq(440.0));
    return &t12;
}

MEL_TEST(musictheory, pitch_pc_bi_negative)
{
    Mel_Pitch p = mel_pitch_make(edo12(), -1);
    MEL_EXPECT_EQ(mel_pitch_pc_index(p), 11);
    MEL_EXPECT_EQ(mel_pitch_bi_index(p), -1);

    p = mel_pitch_make(edo12(), -12);
    MEL_EXPECT_EQ(mel_pitch_pc_index(p), 0);
    MEL_EXPECT_EQ(mel_pitch_bi_index(p), -1);

    p = mel_pitch_make(edo12(), 13);
    MEL_EXPECT_EQ(mel_pitch_pc_index(p), 1);
    MEL_EXPECT_EQ(mel_pitch_bi_index(p), 1);
}

MEL_TEST(musictheory, interval_algebra)
{
    Mel_Interval i = mel_interval_from_pitches(mel_pitch_make(edo12(), 0), mel_pitch_make(edo12(), 7));
    MEL_EXPECT_EQ(i.diff, 7);
    MEL_EXPECT(fabs(mel_cent_to_double(mel_interval_cents(i)) - 700.0) < 1e-6);

    Mel_Interval n = mel_interval_negate(i);
    MEL_EXPECT_EQ(n.diff, -7);
    MEL_EXPECT_EQ(n.ref_index, 7);

    Mel_Interval sum = mel_interval_add(i, mel_interval_make(edo12(), 0, 5));
    MEL_EXPECT_EQ(sum.diff, 12);
}

MEL_TEST(musictheory, scale_sorted_unique)
{
    Mel_Scale s = mel_scale_make(mel_alloc_heap(), edo12());
    mel_scale_add_index(&s, 4);
    mel_scale_add_index(&s, 2);
    mel_scale_add_index(&s, 2);
    mel_scale_add_index(&s, 0);
    MEL_EXPECT_EQ(mel_scale_count(&s), 3);
    MEL_EXPECT_EQ(mel_scale_index_at(&s, 0), 0);
    MEL_EXPECT_EQ(mel_scale_index_at(&s, 1), 2);
    MEL_EXPECT_EQ(mel_scale_index_at(&s, 2), 4);
    mel_scale_free(&s);
}

MEL_TEST(musictheory, scale_set_ops)
{
    Mel_Scale a = mel_scale_from_indices(mel_alloc_heap(), edo12(), (i64[]){ 0, 2, 4 }, 3);
    Mel_Scale b = mel_scale_from_indices(mel_alloc_heap(), edo12(), (i64[]){ 2, 4, 5 }, 3);

    Mel_Scale u = mel_scale_union(mel_alloc_heap(), &a, &b);
    MEL_EXPECT_EQ(mel_scale_count(&u), 4);
    MEL_EXPECT(mel_scale_contains_index(&u, 0) && mel_scale_contains_index(&u, 5));

    Mel_Scale i = mel_scale_intersection(mel_alloc_heap(), &a, &b);
    MEL_EXPECT_EQ(mel_scale_count(&i), 2);
    MEL_EXPECT(mel_scale_contains_index(&i, 2) && mel_scale_contains_index(&i, 4));

    Mel_Scale d = mel_scale_difference(mel_alloc_heap(), &a, &b);
    MEL_EXPECT_EQ(mel_scale_count(&d), 1);
    MEL_EXPECT(mel_scale_contains_index(&d, 0));

    MEL_EXPECT(mel_scale_is_subset(&i, &a));
    MEL_EXPECT(!mel_scale_is_subset(&a, &b));

    mel_scale_free(&a);
    mel_scale_free(&b);
    mel_scale_free(&u);
    mel_scale_free(&i);
    mel_scale_free(&d);
}

MEL_TEST(musictheory, scale_pitches_generator)
{
    Mel_Scale s = mel_scale_from_indices(mel_alloc_heap(), edo12(), (i64[]){ 0, 4, 7 }, 3);

    Mel_Coro_Frame_mel_scale_pitches_g f = { 0 };
    f.s = &s;

    Mel_Pitch p;
    i64       expected[] = { 0, 4, 7 };
    i32       n = 0;
    while (mel_scale_pitches_g__resume(&f, &p))
    {
        MEL_EXPECT_EQ(p.index, expected[n]);
        n++;
    }
    MEL_EXPECT_EQ(n, 3);
    mel_scale_free(&s);
}

MEL_TEST(musictheory, scale_stream_generator)
{
    Mel_Scale s = mel_scale_from_indices(mel_alloc_heap(), edo12(), (i64[]){ 0, 2, 4 }, 3);

    Mel_Coro_Frame_mel_scale_stream_g f = { 0 };
    f.s = &s;
    f.from_index = 0;

    i64       expected[] = { 0, 2, 4, 12, 14, 16, 24 };
    Mel_Pitch p;
    for (i32 n = 0; n < 7; n++)
    {
        MEL_REQUIRE(mel_scale_stream_g__resume(&f, &p));
        MEL_EXPECT_EQ(p.index, expected[n]);
    }

    Mel_Coro_Frame_mel_scale_stream_g f2 = { 0 };
    f2.s = &s;
    f2.from_index = 5;
    MEL_REQUIRE(mel_scale_stream_g__resume(&f2, &p));
    MEL_EXPECT_EQ(p.index, 12);

    mel_scale_free(&s);
}

MEL_TEST(musictheory, scale_complement)
{
    Mel_Scale major = mel_scale_from_indices(mel_alloc_heap(), edo12(), (i64[]){ 0, 2, 4, 5, 7, 9, 11 }, 7);
    Mel_Scale c = mel_scale_pcs_complement(mel_alloc_heap(), &major);
    MEL_EXPECT_EQ(mel_scale_count(&c), 5);
    i64 expected[] = { 1, 3, 6, 8, 10 };
    for (i32 i = 0; i < 5; i++)
        MEL_EXPECT_EQ(mel_scale_index_at(&c, i), expected[i]);
    mel_scale_free(&major);
    mel_scale_free(&c);
}

MEL_TEST(musictheory, scale_normalization_and_rotation)
{
    Mel_Scale s = mel_scale_from_indices(mel_alloc_heap(), edo12(), (i64[]){ 0, 14, 7 }, 3);
    Mel_Scale pn = mel_scale_period_normalized(mel_alloc_heap(), &s);
    MEL_EXPECT_EQ(mel_scale_count(&pn), 3);
    MEL_EXPECT_EQ(mel_scale_index_at(&pn, 0), 0);
    MEL_EXPECT_EQ(mel_scale_index_at(&pn, 1), 2);
    MEL_EXPECT_EQ(mel_scale_index_at(&pn, 2), 7);

    Mel_Scale tri = mel_scale_from_indices(mel_alloc_heap(), edo12(), (i64[]){ 0, 2, 4 }, 3);
    Mel_Scale up = mel_scale_rotated_up(mel_alloc_heap(), &tri);
    MEL_EXPECT_EQ(mel_scale_index_at(&up, 0), 2);
    MEL_EXPECT_EQ(mel_scale_index_at(&up, 1), 4);
    MEL_EXPECT_EQ(mel_scale_index_at(&up, 2), 12);

    Mel_Scale down = mel_scale_rotated_down(mel_alloc_heap(), &up);
    MEL_EXPECT(mel_scale_eq(&down, &tri));

    mel_scale_free(&s);
    mel_scale_free(&pn);
    mel_scale_free(&tri);
    mel_scale_free(&up);
    mel_scale_free(&down);
}

MEL_TEST(musictheory, pattern_from_ratios_quantizes_steps)
{
    Mel_Pattern p = mel_chord_pattern_major_triad(mel_alloc_heap(), edo12());
    MEL_EXPECT_EQ(mel_pattern_count(&p), 2);
    MEL_EXPECT_EQ(mel_pattern_get(&p, 0), 4);
    MEL_EXPECT_EQ(mel_pattern_get(&p, 1), 3);

    Mel_Scale s = mel_pattern_to_scale(mel_alloc_heap(), &p, mel_pitch_make(edo12(), 0));
    MEL_EXPECT_EQ(mel_scale_count(&s), 3);
    MEL_EXPECT(mel_scale_contains_index(&s, 0) && mel_scale_contains_index(&s, 4) && mel_scale_contains_index(&s, 7));

    mel_pattern_free(&p);
    mel_scale_free(&s);
}

MEL_TEST(musictheory, pattern_from_cents)
{
    f64         cents[] = { 700.0 };
    Mel_Pattern p = mel_pattern_from_cents(mel_alloc_heap(), edo12(), cents, 1);
    MEL_EXPECT_EQ(mel_pattern_get(&p, 0), 7);
    mel_pattern_free(&p);
}

MEL_TEST(musictheory, pattern_pitches_generator)
{
    Mel_Pattern p = mel_pattern_from_diffs(mel_alloc_heap(), edo12(), (i64[]){ 4, 3 }, 2);

    Mel_Coro_Frame_mel_pattern_pitches_g f = { 0 };
    f.p = &p;
    f.root = mel_pitch_make(edo12(), 60);

    i64       expected[] = { 60, 64, 67 };
    Mel_Pitch out;
    i32       n = 0;
    while (mel_pattern_pitches_g__resume(&f, &out))
    {
        MEL_EXPECT_EQ(out.index, expected[n]);
        n++;
    }
    MEL_EXPECT_EQ(n, 3);
    mel_pattern_free(&p);
}

MEL_TEST(musictheory, chord_basics)
{
    Mel_Pattern p = mel_chord_pattern_minor_triad(mel_alloc_heap(), edo12());
    Mel_Chord   c = mel_chord_from_root(mel_alloc_heap(), mel_pitch_make(edo12(), 9), &p);
    MEL_EXPECT_EQ(mel_chord_size(&c), 3);
    MEL_EXPECT_EQ(mel_chord_root(&c).index, 9);
    MEL_EXPECT_EQ(mel_chord_at(&c, 0).index, 9);
    MEL_EXPECT_EQ(mel_chord_at(&c, 1).index, 12);
    MEL_EXPECT_EQ(mel_chord_at(&c, 2).index, 16);
    mel_pattern_free(&p);
    mel_chord_free(&c);
}
