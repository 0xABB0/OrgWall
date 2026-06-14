#pragma once

#include <core/compiler.h>
#include <core/types.h>
#include <allocator/allocator.h>
#include <collection/array.h>
#include <string/str8.h>
#include <musictheory/scale.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct Mel_Chord_Quality Mel_Chord_Quality;

struct Mel_Chord_Quality
{
    str8            name;
    Mel_Index_Array diffs;
};

typedef Mel_Array(Mel_Chord_Quality) Mel_Chord_Quality_Array;

typedef struct Mel_Chord_Catalog Mel_Chord_Catalog;

struct Mel_Chord_Catalog
{
    const Mel_Alloc*        alloc;
    Mel_Chord_Quality_Array entries;
};

void               mel_chord_catalog_free(Mel_Chord_Catalog* cat);
static inline void mel_chord_catalog_cleanup(Mel_Chord_Catalog* cat) { mel_chord_catalog_free(cat); }
#define Mel_Chord_Catalog_AUTO MEL_CLEANUP(mel_chord_catalog_cleanup) Mel_Chord_Catalog

MEL_NODISCARD Mel_Chord_Catalog mel_chord_catalog_make(const Mel_Alloc* alloc);

void mel_chord_catalog_add(Mel_Chord_Catalog* cat, str8 name, const i64* diffs, i32 count);

typedef struct Mel_Chord_Match Mel_Chord_Match;

struct Mel_Chord_Match
{
    i64 root_pc;
    i32 quality;
    i32 bass_member;
};

typedef Mel_Array(Mel_Chord_Match) Mel_Chord_Match_Array;

MEL_NODISCARD static inline bool mel_chord_quality_matches(const Mel_Chord_Quality* q, const Mel_Scale* pcs, i32 root_pos, i64 period)
{
    i32 n = mel_scale_count(pcs);
    if ((i32)q->diffs.count != n - 1)
        return false;
    i64 root = pcs->indices.items[root_pos];
    for (i32 j = 1; j < n; j++)
    {
        i64 pc = pcs->indices.items[(root_pos + j) % n];
        i64 diff = (pc - root) % period;
        if (diff < 0)
            diff += period;
        if (diff != q->diffs.items[j - 1])
            return false;
    }
    return true;
}

MEL_NODISCARD static inline i32 mel_chord_bass_member(const Mel_Scale* pcs, i32 root_pos, i64 bass_pc)
{
    i32 n = mel_scale_count(pcs);
    for (i32 j = 0; j < n; j++)
        if (pcs->indices.items[(root_pos + j) % n] == bass_pc)
            return j;
    return -1;
}

MEL_NODISCARD Mel_Chord_Match_Array mel_chord_identify(const Mel_Alloc* alloc, const Mel_Chord_Catalog* cat, const Mel_Scale* pcs, i64 bass_pc);

#ifdef __cplusplus
}
#endif
