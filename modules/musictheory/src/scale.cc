#include <musictheory/pattern.h>
#include <musictheory/scale.h>

#include <cppcoro/generator.hpp>

#include <assert.h>

namespace
{

cppcoro::generator<i64> scale_union_g(const Mel_Scale* a, const Mel_Scale* b)
{
    usize ia = 0;
    usize ib = 0;
    while (ia < a->indices.count && ib < b->indices.count)
    {
        if (a->indices.items[ia] == b->indices.items[ib])
        {
            co_yield a->indices.items[ia];
            ia++;
            ib++;
        }
        else if (a->indices.items[ia] < b->indices.items[ib])
        {
            co_yield a->indices.items[ia];
            ia++;
        }
        else
        {
            co_yield b->indices.items[ib];
            ib++;
        }
    }
    while (ia < a->indices.count)
    {
        co_yield a->indices.items[ia];
        ia++;
    }
    while (ib < b->indices.count)
    {
        co_yield b->indices.items[ib];
        ib++;
    }
}

cppcoro::generator<i64> scale_intersection_g(const Mel_Scale* a, const Mel_Scale* b)
{
    usize ia = 0;
    usize ib = 0;
    while (ia < a->indices.count && ib < b->indices.count)
    {
        if (a->indices.items[ia] == b->indices.items[ib])
        {
            co_yield a->indices.items[ia];
            ia++;
            ib++;
        }
        else if (a->indices.items[ia] < b->indices.items[ib])
        {
            ia++;
        }
        else
        {
            ib++;
        }
    }
}

cppcoro::generator<i64> scale_difference_g(const Mel_Scale* a, const Mel_Scale* b)
{
    usize ia = 0;
    usize ib = 0;
    while (ia < a->indices.count && ib < b->indices.count)
    {
        if (a->indices.items[ia] == b->indices.items[ib])
        {
            ia++;
            ib++;
        }
        else if (a->indices.items[ia] < b->indices.items[ib])
        {
            co_yield a->indices.items[ia];
            ia++;
        }
        else
        {
            ib++;
        }
    }
    while (ia < a->indices.count)
    {
        co_yield a->indices.items[ia];
        ia++;
    }
}

cppcoro::generator<i64> scale_complement_g(const Mel_Scale* s)
{
    for (i64 pc = 0; pc < (i64)s->tuning->period; pc++)
        if (!mel_scale_contains_pc(s, pc))
            co_yield pc;
}

cppcoro::generator<Mel_Pitch> scale_pitches_g(const Mel_Scale* s)
{
    for (usize i = 0; i < s->indices.count; i++)
        co_yield mel_pitch_make(s->tuning, s->indices.items[i]);
}

cppcoro::generator<Mel_Pitch> scale_stream_g(const Mel_Scale* s, i64 from_index)
{
    i64 period = (i64)s->tuning->period;
    i64 start_bi = from_index >= 0 ? from_index / period : -((-from_index + period - 1) / period);
    for (i64 bi = start_bi;; bi++)
        for (usize i = 0; i < s->indices.count; i++)
        {
            i64 idx = s->indices.items[i] + bi * period;
            if (idx >= from_index)
                co_yield mel_pitch_make(s->tuning, idx);
        }
}

cppcoro::generator<Mel_Pitch> pattern_pitches_g(const Mel_Pattern* p, Mel_Pitch root)
{
    co_yield root;
    i64 idx = root.index;
    for (usize i = 0; i < p->diffs.count; i++)
    {
        idx += p->diffs.items[i];
        co_yield mel_pitch_make(root.tuning, idx);
    }
}

}

i32 mel_scale_pitches(const Mel_Scale* s, Mel_Pitch* out, i32 cap)
{
    i32 n = 0;
    for (Mel_Pitch p : scale_pitches_g(s))
    {
        if (n >= cap)
            break;
        out[n++] = p;
    }
    return n;
}

i32 mel_scale_stream(const Mel_Scale* s, i64 from_index, Mel_Pitch* out, i32 count)
{
    i32 n = 0;
    for (Mel_Pitch p : scale_stream_g(s, from_index))
    {
        if (n >= count)
            break;
        out[n++] = p;
    }
    return n;
}

i32 mel_pattern_pitches(const Mel_Pattern* p, Mel_Pitch root, Mel_Pitch* out, i32 cap)
{
    i32 n = 0;
    for (Mel_Pitch pitch : pattern_pitches_g(p, root))
    {
        if (n >= cap)
            break;
        out[n++] = pitch;
    }
    return n;
}

void mel_scale_free(Mel_Scale* s)
{
    if (!s)
        return;
    mel_array_free(&s->indices);
}

Mel_Scale mel_scale_make(const Mel_Alloc* alloc, const Mel_Tuning* tuning)
{
    assert(alloc && tuning);
    Mel_Scale s;
    s.tuning = tuning;
    mel_array_init(&s.indices, alloc);
    return s;
}

Mel_Scale mel_scale_from_indices(const Mel_Alloc* alloc, const Mel_Tuning* tuning, const i64* indices, i32 count)
{
    Mel_Scale s = mel_scale_make(alloc, tuning);
    for (i32 i = 0; i < count; i++)
        mel_scale_add_index(&s, indices[i]);
    return s;
}

Mel_Scale mel_scale_from_pitches(const Mel_Alloc* alloc, const Mel_Tuning* tuning, const Mel_Pitch* pitches, i32 count)
{
    Mel_Scale s = mel_scale_make(alloc, tuning);
    for (i32 i = 0; i < count; i++)
        mel_scale_add_pitch(&s, pitches[i]);
    return s;
}

Mel_Scale mel_scale_copy(const Mel_Alloc* alloc, const Mel_Scale* s)
{
    Mel_Scale c = mel_scale_make(alloc, s->tuning);
    mel_array_reserve(&c.indices, s->indices.count);
    for (usize i = 0; i < s->indices.count; i++)
        mel_array_push(&c.indices, s->indices.items[i]);
    return c;
}

i64 mel_scale_index_at(const Mel_Scale* s, i32 idx)
{
    assert(idx >= 0 && (usize)idx < s->indices.count);
    return s->indices.items[idx];
}

Mel_Pitch mel_scale_at(const Mel_Scale* s, i32 idx) { return mel_pitch_make(s->tuning, mel_scale_index_at(s, idx)); }

static usize mel_scale__lower_bound(const Mel_Scale* s, i64 index)
{
    usize lo = 0, hi = s->indices.count;
    while (lo < hi)
    {
        usize mid = lo + (hi - lo) / 2;
        if (s->indices.items[mid] < index)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

void mel_scale_add_index(Mel_Scale* s, i64 index)
{
    usize pos = mel_scale__lower_bound(s, index);
    if (pos < s->indices.count && s->indices.items[pos] == index)
        return;
    mel_array_insert(&s->indices, pos, index);
}

void mel_scale_add_pitch(Mel_Scale* s, Mel_Pitch pitch)
{
    assert(pitch.tuning == s->tuning);
    mel_scale_add_index(s, pitch.index);
}

u8 mel_scale_contains_index(const Mel_Scale* s, i64 index)
{
    usize pos = mel_scale__lower_bound(s, index);
    return pos < s->indices.count && s->indices.items[pos] == index ? 1 : 0;
}

u8 mel_scale_contains_pitch(const Mel_Scale* s, Mel_Pitch pitch)
{
    assert(pitch.tuning == s->tuning);
    return mel_scale_contains_index(s, pitch.index);
}

u8 mel_scale_contains_pc(const Mel_Scale* s, i64 pc)
{
    for (usize i = 0; i < s->indices.count; i++)
        if (mel_pitch_pc_index(mel_scale_at(s, (i32)i)) == pc)
            return 1;
    return 0;
}

Mel_Interval mel_scale_spec_interval(const Mel_Scale* s, i32 source_idx, i32 target_idx) { return mel_interval_from_pitches(mel_scale_at(s, source_idx), mel_scale_at(s, target_idx)); }

void mel_scale_indices(const Mel_Scale* s, i64* out_indices)
{
    for (usize i = 0; i < s->indices.count; i++)
        out_indices[i] = s->indices.items[i];
}

Mel_Scale mel_scale_transpose(const Mel_Alloc* alloc, const Mel_Scale* s, i64 diff)
{
    Mel_Scale r = mel_scale_make(alloc, s->tuning);
    mel_array_reserve(&r.indices, s->indices.count);
    for (usize i = 0; i < s->indices.count; i++)
        mel_array_push(&r.indices, s->indices.items[i] + diff);
    return r;
}

Mel_Scale mel_scale_union(const Mel_Alloc* alloc, const Mel_Scale* a, const Mel_Scale* b)
{
    assert(a->tuning == b->tuning);
    Mel_Scale r = mel_scale_make(alloc, a->tuning);
    for (i64 idx : scale_union_g(a, b))
        mel_array_push(&r.indices, idx);
    return r;
}

Mel_Scale mel_scale_intersection(const Mel_Alloc* alloc, const Mel_Scale* a, const Mel_Scale* b)
{
    assert(a->tuning == b->tuning);
    Mel_Scale r = mel_scale_make(alloc, a->tuning);
    for (i64 idx : scale_intersection_g(a, b))
        mel_array_push(&r.indices, idx);
    return r;
}

Mel_Scale mel_scale_difference(const Mel_Alloc* alloc, const Mel_Scale* a, const Mel_Scale* b)
{
    assert(a->tuning == b->tuning);
    Mel_Scale r = mel_scale_make(alloc, a->tuning);
    for (i64 idx : scale_difference_g(a, b))
        mel_array_push(&r.indices, idx);
    return r;
}

u8 mel_scale_is_subset(const Mel_Scale* a, const Mel_Scale* b)
{
    if (a->indices.count > b->indices.count)
        return 0;
    usize bi = 0;
    for (usize ai = 0; ai < a->indices.count; ai++)
    {
        while (bi < b->indices.count && b->indices.items[bi] < a->indices.items[ai])
            bi++;
        if (bi >= b->indices.count || b->indices.items[bi] != a->indices.items[ai])
            return 0;
        bi++;
    }
    return 1;
}

u8 mel_scale_eq(const Mel_Scale* a, const Mel_Scale* b)
{
    if (a->tuning != b->tuning || a->indices.count != b->indices.count)
        return 0;
    for (usize i = 0; i < a->indices.count; i++)
        if (a->indices.items[i] != b->indices.items[i])
            return 0;
    return 1;
}

u8 mel_scale_is_set_equivalent(const Mel_Scale* a, const Mel_Scale* b)
{
    for (usize i = 0; i < a->indices.count; i++)
        if (!mel_scale_contains_pc(b, mel_pitch_pc_index(mel_scale_at(a, (i32)i))))
            return 0;
    for (usize i = 0; i < b->indices.count; i++)
        if (!mel_scale_contains_pc(a, mel_pitch_pc_index(mel_scale_at(b, (i32)i))))
            return 0;
    return 1;
}

Mel_Scale mel_scale_reflect(const Mel_Alloc* alloc, const Mel_Scale* s, Mel_Pitch axis)
{
    assert(axis.tuning == s->tuning);
    Mel_Scale r = mel_scale_make(alloc, s->tuning);
    for (usize i = 0; i < s->indices.count; i++)
        mel_scale_add_index(&r, 2 * axis.index - s->indices.items[i]);
    return r;
}

Mel_Scale mel_scale_zero_normalized(const Mel_Alloc* alloc, const Mel_Scale* s)
{
    if (s->indices.count == 0)
        return mel_scale_make(alloc, s->tuning);
    return mel_scale_transpose(alloc, s, -s->indices.items[0]);
}

Mel_Scale mel_scale_pcs_normalized(const Mel_Alloc* alloc, const Mel_Scale* s)
{
    Mel_Scale r = mel_scale_make(alloc, s->tuning);
    for (usize i = 0; i < s->indices.count; i++)
        mel_scale_add_index(&r, mel_pitch_pc_index(mel_scale_at(s, (i32)i)));
    return r;
}

Mel_Scale mel_scale_period_normalized(const Mel_Alloc* alloc, const Mel_Scale* s)
{
    assert(mel_tuning_is_periodic(s->tuning));
    Mel_Scale r = mel_scale_make(alloc, s->tuning);
    if (s->indices.count == 0)
        return r;

    i64 period = (i64)s->tuning->period;
    i64 root = s->indices.items[0];
    mel_scale_add_index(&r, root);

    for (usize i = 1; i < s->indices.count; i++)
    {
        i64 pc_diff = (s->indices.items[i] - root) % period;
        if (pc_diff < 0)
            pc_diff += period;
        if (pc_diff == 0)
            continue;
        mel_scale_add_index(&r, root + pc_diff);
    }
    return r;
}

Mel_Scale mel_scale_pcs_complement(const Mel_Alloc* alloc, const Mel_Scale* s)
{
    assert(mel_tuning_is_periodic(s->tuning));
    Mel_Scale r = mel_scale_make(alloc, s->tuning);
    for (i64 pc : scale_complement_g(s))
        mel_array_push(&r.indices, pc);
    return r;
}

Mel_Scale mel_scale_rotated_up(const Mel_Alloc* alloc, const Mel_Scale* s)
{
    assert(mel_tuning_is_periodic(s->tuning));
    if (s->indices.count == 0)
        return mel_scale_make(alloc, s->tuning);

    i64 period = (i64)s->tuning->period;
    i64 first = s->indices.items[0];
    i64 last = s->indices.items[s->indices.count - 1];
    i64 lifted = first + ((last - first) / period + 1) * period;

    Mel_Scale r = mel_scale_make(alloc, s->tuning);
    for (usize i = 1; i < s->indices.count; i++)
        mel_scale_add_index(&r, s->indices.items[i]);
    mel_scale_add_index(&r, lifted);
    return r;
}

Mel_Scale mel_scale_rotated_down(const Mel_Alloc* alloc, const Mel_Scale* s)
{
    assert(mel_tuning_is_periodic(s->tuning));
    if (s->indices.count == 0)
        return mel_scale_make(alloc, s->tuning);

    i64 period = (i64)s->tuning->period;
    i64 first = s->indices.items[0];
    i64 last = s->indices.items[s->indices.count - 1];
    i64 dropped = last - ((last - first) / period + 1) * period;

    Mel_Scale r = mel_scale_make(alloc, s->tuning);
    mel_scale_add_index(&r, dropped);
    for (usize i = 0; i + 1 < s->indices.count; i++)
        mel_scale_add_index(&r, s->indices.items[i]);
    return r;
}

Mel_Scale mel_scale_rotation(const Mel_Alloc* alloc, const Mel_Scale* s, i32 order)
{
    Mel_Scale r = mel_scale_copy(alloc, s);
    for (i32 i = 0; i < (order >= 0 ? order : -order); i++)
    {
        Mel_Scale next = order >= 0 ? mel_scale_rotated_up(alloc, &r) : mel_scale_rotated_down(alloc, &r);
        mel_scale_free(&r);
        r = next;
    }
    return r;
}
