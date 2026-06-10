#include <notation/chord_id.h>
#include <notation/identify.coro.h>

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
    const Mel_Chord_Match* ma = a;
    const Mel_Chord_Match* mb = b;
    if ((ma->bass_member == 0) != (mb->bass_member == 0))
        return ma->bass_member == 0 ? -1 : 1;
    if (ma->quality != mb->quality)
        return ma->quality - mb->quality;
    return ma->root_pc < mb->root_pc ? -1 : (ma->root_pc > mb->root_pc ? 1 : 0);
}

Mel_Chord_Match_Array mel_chord_identify(const Mel_Alloc* alloc, const Mel_Chord_Catalog* cat, const Mel_Scale* pcs, i64 bass_pc)
{
    Mel_Chord_Match_Array matches;
    mel_array_init(&matches, alloc);

    Mel_Coro_Frame_mel_chord_identify_g f = { 0 };
    f.cat = cat;
    f.pcs = pcs;
    f.bass_pc = bass_pc;

    Mel_Chord_Match m;
    while (mel_chord_identify_g__resume(&f, &m))
        mel_array_push(&matches, m);

    if (matches.count > 1)
        qsort(matches.items, matches.count, sizeof(Mel_Chord_Match), mel_chord_match__cmp);

    return matches;
}
