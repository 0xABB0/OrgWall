#include <musicnotation/chord_id.h>

#include <cppcoro/generator.hpp>

#include <assert.h>
#include <stdlib.h>

void mel_chord_catalog_free(Mel_Chord_Catalog* cat)
{
    if (!cat)
        return;
    for (usize i = 0; i < cat->entries.count; i++)
    {
        mel_dealloc(cat->alloc, cat->entries.items[i].name.data);
        mel_array_free(&cat->entries.items[i].diffs);
    }
    mel_array_free(&cat->entries);
}

Mel_Chord_Catalog mel_chord_catalog_make(const Mel_Alloc* alloc)
{
    assert(alloc);
    Mel_Chord_Catalog cat;
    cat.alloc = alloc;
    mel_array_init(&cat.entries, alloc);
    return cat;
}

void mel_chord_catalog_add(Mel_Chord_Catalog* cat, str8 name, const i64* diffs, i32 count)
{
    assert(count > 0);
    Mel_Chord_Quality q;
    q.name = str8_dup_alloc(name, cat->alloc);
    mel_array_init(&q.diffs, cat->alloc);
    mel_array_reserve(&q.diffs, (usize)count);
    for (i32 i = 0; i < count; i++)
    {
        assert(i == 0 || diffs[i] > diffs[i - 1]);
        mel_array_push(&q.diffs, diffs[i]);
    }
    mel_array_push(&cat->entries, q);
}

static int mel_chord_match__cmp(const void* a, const void* b)
{
    const Mel_Chord_Match* ma = (const Mel_Chord_Match*)a;
    const Mel_Chord_Match* mb = (const Mel_Chord_Match*)b;
    if ((ma->bass_member == 0) != (mb->bass_member == 0))
        return ma->bass_member == 0 ? -1 : 1;
    if (ma->quality != mb->quality)
        return ma->quality - mb->quality;
    return ma->root_pc < mb->root_pc ? -1 : (ma->root_pc > mb->root_pc ? 1 : 0);
}

static cppcoro::generator<Mel_Chord_Match> mel_chord_identify_g(const Mel_Chord_Catalog* cat, const Mel_Scale* pcs, i64 bass_pc)
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
                co_yield m;
            }
}

Mel_Chord_Match_Array mel_chord_identify(const Mel_Alloc* alloc, const Mel_Chord_Catalog* cat, const Mel_Scale* pcs, i64 bass_pc)
{
    Mel_Chord_Match_Array matches;
    mel_array_init(&matches, alloc);

    for (Mel_Chord_Match m : mel_chord_identify_g(cat, pcs, bass_pc))
        mel_array_push(&matches, m);

    if (matches.count > 1)
        qsort(matches.items, matches.count, sizeof(Mel_Chord_Match), mel_chord_match__cmp);

    return matches;
}
