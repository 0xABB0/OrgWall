#pragma once

#include <coro/coro.h>
#include <core/types.h>

#include "pitch.h"
#include "scale.h"
#include "pattern.h"

/* >>> mel_coro generated frames — managed region, do not edit >>> */
typedef struct Mel_Coro_Frame_mel_scale_pitches_g
{
    i32 state;
    const Mel_Scale * s;
    usize i;
    Mel_Pitch __ret;
} Mel_Coro_Frame_mel_scale_pitches_g;

#define MEL_CORO_LAYOUT_HASH_mel_scale_pitches_g 0x4b0b52e2fcec8492ull

Mel_Coro_Suspended mel_scale_pitches_g__resume(Mel_Coro_Frame_mel_scale_pitches_g* __f, struct Mel_Pitch* __f_out);

typedef struct Mel_Coro_Frame_mel_scale_union_g
{
    i32 state;
    const Mel_Scale * a;
    const Mel_Scale * b;
    usize ia;
    usize ib;
    i64 __ret;
} Mel_Coro_Frame_mel_scale_union_g;

#define MEL_CORO_LAYOUT_HASH_mel_scale_union_g 0x2d529783ff9c5820ull

Mel_Coro_Suspended mel_scale_union_g__resume(Mel_Coro_Frame_mel_scale_union_g* __f, long long* __f_out);

typedef struct Mel_Coro_Frame_mel_scale_intersection_g
{
    i32 state;
    const Mel_Scale * a;
    const Mel_Scale * b;
    usize ia;
    usize ib;
    i64 __ret;
} Mel_Coro_Frame_mel_scale_intersection_g;

#define MEL_CORO_LAYOUT_HASH_mel_scale_intersection_g 0x2d529783ff9c5820ull

Mel_Coro_Suspended mel_scale_intersection_g__resume(Mel_Coro_Frame_mel_scale_intersection_g* __f, long long* __f_out);

typedef struct Mel_Coro_Frame_mel_scale_difference_g
{
    i32 state;
    const Mel_Scale * a;
    const Mel_Scale * b;
    usize ia;
    usize ib;
    i64 __ret;
} Mel_Coro_Frame_mel_scale_difference_g;

#define MEL_CORO_LAYOUT_HASH_mel_scale_difference_g 0x2d529783ff9c5820ull

Mel_Coro_Suspended mel_scale_difference_g__resume(Mel_Coro_Frame_mel_scale_difference_g* __f, long long* __f_out);

typedef struct Mel_Coro_Frame_mel_scale_complement_g
{
    i32 state;
    const Mel_Scale * s;
    i64 pc;
    i64 __ret;
} Mel_Coro_Frame_mel_scale_complement_g;

#define MEL_CORO_LAYOUT_HASH_mel_scale_complement_g 0xefef3a141ea0f7f3ull

Mel_Coro_Suspended mel_scale_complement_g__resume(Mel_Coro_Frame_mel_scale_complement_g* __f, long long* __f_out);

typedef struct Mel_Coro_Frame_mel_scale_stream_g
{
    i32 state;
    const Mel_Scale * s;
    i64 from_index;
    i64 period;
    i64 start_bi;
    i64 bi;
    usize i;
    i64 idx;
    Mel_Pitch __ret;
} Mel_Coro_Frame_mel_scale_stream_g;

#define MEL_CORO_LAYOUT_HASH_mel_scale_stream_g 0x49dbb0b71d7c73c6ull

Mel_Coro_Suspended mel_scale_stream_g__resume(Mel_Coro_Frame_mel_scale_stream_g* __f, struct Mel_Pitch* __f_out);

typedef struct Mel_Coro_Frame_mel_pattern_pitches_g
{
    i32 state;
    const Mel_Pattern * p;
    Mel_Pitch root;
    i64 idx;
    usize i;
    Mel_Pitch __ret;
} Mel_Coro_Frame_mel_pattern_pitches_g;

#define MEL_CORO_LAYOUT_HASH_mel_pattern_pitches_g 0x9afe9578a66db18cull

Mel_Coro_Suspended mel_pattern_pitches_g__resume(Mel_Coro_Frame_mel_pattern_pitches_g* __f, struct Mel_Pitch* __f_out);

/* <<< mel_coro generated frames <<< */

mel_coro(mel_scale_pitches_g, (const Mel_Scale* s), Mel_Pitch)
{
    for (usize i = 0; i < s->indices.count; i++)
        mel_coro_yield(mel_pitch_make(s->tuning, s->indices.items[i]));
    mel_coro_return(((Mel_Pitch){ 0 }));
}

mel_coro(mel_scale_union_g, (const Mel_Scale* a, const Mel_Scale* b), i64)
{
    usize ia = 0;
    usize ib = 0;
    while (ia < a->indices.count && ib < b->indices.count)
    {
        if (a->indices.items[ia] == b->indices.items[ib])
        {
            mel_coro_yield(a->indices.items[ia]);
            ia++;
            ib++;
        }
        else if (a->indices.items[ia] < b->indices.items[ib])
        {
            mel_coro_yield(a->indices.items[ia]);
            ia++;
        }
        else
        {
            mel_coro_yield(b->indices.items[ib]);
            ib++;
        }
    }
    while (ia < a->indices.count)
    {
        mel_coro_yield(a->indices.items[ia]);
        ia++;
    }
    while (ib < b->indices.count)
    {
        mel_coro_yield(b->indices.items[ib]);
        ib++;
    }
    mel_coro_return(0);
}

mel_coro(mel_scale_intersection_g, (const Mel_Scale* a, const Mel_Scale* b), i64)
{
    usize ia = 0;
    usize ib = 0;
    while (ia < a->indices.count && ib < b->indices.count)
    {
        if (a->indices.items[ia] == b->indices.items[ib])
        {
            mel_coro_yield(a->indices.items[ia]);
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
    mel_coro_return(0);
}

mel_coro(mel_scale_difference_g, (const Mel_Scale* a, const Mel_Scale* b), i64)
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
            mel_coro_yield(a->indices.items[ia]);
            ia++;
        }
        else
        {
            ib++;
        }
    }
    while (ia < a->indices.count)
    {
        mel_coro_yield(a->indices.items[ia]);
        ia++;
    }
    mel_coro_return(0);
}

mel_coro(mel_scale_complement_g, (const Mel_Scale* s), i64)
{
    for (i64 pc = 0; pc < (i64)s->tuning->period; pc++)
        if (!mel_scale_contains_pc(s, pc))
            mel_coro_yield(pc);
    mel_coro_return(0);
}

mel_coro(mel_scale_stream_g, (const Mel_Scale* s, i64 from_index), Mel_Pitch)
{
    i64 period = (i64)s->tuning->period;
    i64 start_bi = from_index >= 0 ? from_index / period : -((-from_index + period - 1) / period);
    for (i64 bi = start_bi;; bi++)
        for (usize i = 0; i < s->indices.count; i++)
        {
            i64 idx = s->indices.items[i] + bi * period;
            if (idx >= from_index)
                mel_coro_yield(mel_pitch_make(s->tuning, idx));
        }
    mel_coro_return(((Mel_Pitch){ 0 }));
}

mel_coro(mel_pattern_pitches_g, (const Mel_Pattern* p, Mel_Pitch root), Mel_Pitch)
{
    mel_coro_yield(root);
    i64 idx = root.index;
    for (usize i = 0; i < p->diffs.count; i++)
    {
        idx += p->diffs.items[i];
        mel_coro_yield(mel_pitch_make(root.tuning, idx));
    }
    mel_coro_return(((Mel_Pitch){ 0 }));
}
