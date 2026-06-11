#pragma once

#include <coro/coro.h>
#include <core/types.h>

#include "chord_id.h"

/* >>> mel_coro generated frames — managed region, do not edit >>> */
typedef struct Mel_Coro_Frame_mel_chord_identify_g
{
    i32 state;
    const Mel_Chord_Catalog * cat;
    const Mel_Scale * pcs;
    i64 bass_pc;
    i64 period;
    i32 n;
    i32 r;
    usize q;
    Mel_Chord_Match m;
    Mel_Chord_Match __ret;
} Mel_Coro_Frame_mel_chord_identify_g;

#define MEL_CORO_LAYOUT_HASH_mel_chord_identify_g 0x52a94d2b309bf42full

Mel_Coro_Suspended mel_chord_identify_g__resume(Mel_Coro_Frame_mel_chord_identify_g* __f, struct Mel_Chord_Match* __f_out);

/* <<< mel_coro generated frames <<< */

mel_coro(mel_chord_identify_g, (const Mel_Chord_Catalog* cat, const Mel_Scale* pcs, i64 bass_pc), Mel_Chord_Match)
{
    i64 period = (i64)pcs->tuning->period;
    i32 n = mel_scale_count(pcs);
    for (i32 r = 0; r < n; r++)
        for (usize q = 0; q < cat->entries.count; q++)
            if (mel_chord_quality_matches(&cat->entries.items[q], pcs, r, period))
            {
                Mel_Chord_Match m;
                m.root_pc = pcs->indices.items[r];
                m.quality = (i32)q;
                m.bass_member = mel_chord_bass_member(pcs, r, bass_pc);
                mel_coro_yield(m);
            }
    mel_coro_return(((Mel_Chord_Match){ 0 }));
}
