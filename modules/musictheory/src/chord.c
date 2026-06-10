#include <musictheory/chord.h>

#include <assert.h>

void mel_chord_free(Mel_Chord* c)
{
    if (!c)
        return;
    mel_scale_free(&c->pitches);
}

Mel_Chord mel_chord_from_root(const Mel_Alloc* alloc, Mel_Pitch root, const Mel_Pattern* pattern)
{
    Mel_Chord c;
    c.pitches = mel_pattern_to_scale(alloc, pattern, root);
    c.root_index = root.index;
    return c;
}

Mel_Chord mel_chord_from_root_and_diffs(const Mel_Alloc* alloc, Mel_Pitch root, const i64* diffs, i32 count)
{
    Mel_Pattern pattern = mel_pattern_from_diffs(alloc, root.tuning, diffs, count);
    Mel_Chord   c = mel_chord_from_root(alloc, root, &pattern);
    mel_pattern_free(&pattern);
    return c;
}

Mel_Chord mel_chord_copy(const Mel_Alloc* alloc, const Mel_Chord* c)
{
    Mel_Chord r;
    r.pitches = mel_scale_copy(alloc, &c->pitches);
    r.root_index = c->root_index;
    return r;
}

Mel_Pitch mel_chord_root(const Mel_Chord* c)
{
    assert(mel_chord_size(c) > 0);
    return mel_pitch_make(c->pitches.tuning, c->root_index);
}

Mel_Pitch mel_chord_at(const Mel_Chord* c, i32 idx) { return mel_scale_at(&c->pitches, idx); }

Mel_Pattern mel_chord_pattern_major_triad(const Mel_Alloc* alloc, const Mel_Tuning* tuning)
{
    u64 num[] = { 5, 6 };
    u64 den[] = { 4, 5 };
    return mel_pattern_from_ratios(alloc, tuning, num, den, 2);
}

Mel_Pattern mel_chord_pattern_minor_triad(const Mel_Alloc* alloc, const Mel_Tuning* tuning)
{
    u64 num[] = { 6, 5 };
    u64 den[] = { 5, 4 };
    return mel_pattern_from_ratios(alloc, tuning, num, den, 2);
}

Mel_Pattern mel_chord_pattern_diminished_triad(const Mel_Alloc* alloc, const Mel_Tuning* tuning)
{
    u64 num[] = { 6, 6 };
    u64 den[] = { 5, 5 };
    return mel_pattern_from_ratios(alloc, tuning, num, den, 2);
}

Mel_Pattern mel_chord_pattern_augmented_triad(const Mel_Alloc* alloc, const Mel_Tuning* tuning)
{
    u64 num[] = { 5, 5 };
    u64 den[] = { 4, 4 };
    return mel_pattern_from_ratios(alloc, tuning, num, den, 2);
}

Mel_Pattern mel_chord_pattern_major_seventh(const Mel_Alloc* alloc, const Mel_Tuning* tuning)
{
    u64 num[] = { 5, 6, 5 };
    u64 den[] = { 4, 5, 4 };
    return mel_pattern_from_ratios(alloc, tuning, num, den, 3);
}

Mel_Pattern mel_chord_pattern_minor_seventh(const Mel_Alloc* alloc, const Mel_Tuning* tuning)
{
    u64 num[] = { 6, 5, 6 };
    u64 den[] = { 5, 4, 5 };
    return mel_pattern_from_ratios(alloc, tuning, num, den, 3);
}

Mel_Pattern mel_chord_pattern_dominant_seventh(const Mel_Alloc* alloc, const Mel_Tuning* tuning)
{
    u64 num[] = { 5, 6, 6 };
    u64 den[] = { 4, 5, 5 };
    return mel_pattern_from_ratios(alloc, tuning, num, den, 3);
}

Mel_Pattern mel_chord_pattern_diminished_seventh(const Mel_Alloc* alloc, const Mel_Tuning* tuning)
{
    u64 num[] = { 6, 6, 6 };
    u64 den[] = { 5, 5, 5 };
    return mel_pattern_from_ratios(alloc, tuning, num, den, 3);
}
