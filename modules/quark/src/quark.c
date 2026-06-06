#include <quark/quark.h>

#include <string/str8.h>
#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <collection/array.h>

#include <string.h>

typedef struct
{
    str8 name;
} Mel_Quark_Entry;

typedef struct
{
    u64       hash;
    Mel_Quark quark;
} Mel_Quark_Bucket;

struct Mel_Quark_Table
{
    const Mel_Alloc* alloc;
    Mel_Array(Mel_Quark_Entry) entries;
    Mel_Quark_Bucket* buckets;
    u32               bucket_count;
    u32               occupied;
};

#define MEL_QUARK_INIT_BUCKETS 16u

static u32 mel__quark_probe(const Mel_Quark_Table* t, u64 hash, str8 needle, bool* out_found)
{
    u32 mask = t->bucket_count - 1u;
    u32 idx = (u32)hash & mask;
    while (t->buckets[idx].quark != MEL_QUARK_NONE)
    {
        if (t->buckets[idx].hash == hash)
        {
            str8 candidate = t->entries.items[t->buckets[idx].quark - 1u].name;
            if (str8_equals(candidate, needle))
            {
                *out_found = true;
                return idx;
            }
        }
        idx = (idx + 1u) & mask;
    }
    *out_found = false;
    return idx;
}

static bool mel__quark_grow(Mel_Quark_Table* t, u32 new_count)
{
    Mel_Quark_Bucket* fresh = mel_alloc(t->alloc, sizeof(Mel_Quark_Bucket) * new_count);
    if (fresh == NULL)
        return false;
    memset(fresh, 0, sizeof(Mel_Quark_Bucket) * new_count);

    Mel_Quark_Bucket* old = t->buckets;
    u32               old_count = t->bucket_count;

    t->buckets = fresh;
    t->bucket_count = new_count;
    u32 mask = new_count - 1u;

    for (u32 i = 0; i < old_count; i++)
    {
        if (old[i].quark == MEL_QUARK_NONE)
            continue;
        u32 idx = (u32)old[i].hash & mask;
        while (t->buckets[idx].quark != MEL_QUARK_NONE)
            idx = (idx + 1u) & mask;
        t->buckets[idx] = old[i];
    }

    mel_dealloc(t->alloc, old);
    return true;
}

Mel_Quark_Table* mel_quark_table_create(const Mel_Alloc* alloc)
{
    if (alloc == NULL)
        alloc = mel_alloc_heap();

    Mel_Quark_Table* t = mel_alloc(alloc, sizeof(Mel_Quark_Table));
    if (t == NULL)
        return NULL;

    t->alloc = alloc;
    mel_array_init(&t->entries, alloc);
    t->bucket_count = MEL_QUARK_INIT_BUCKETS;
    t->buckets = mel_alloc(alloc, sizeof(Mel_Quark_Bucket) * t->bucket_count);
    if (t->buckets == NULL)
    {
        mel_dealloc(alloc, t);
        return NULL;
    }
    memset(t->buckets, 0, sizeof(Mel_Quark_Bucket) * t->bucket_count);
    t->occupied = 0;

    return t;
}

void mel_quark_table_destroy(Mel_Quark_Table* t)
{
    if (t == NULL)
        return;

    const Mel_Alloc* alloc = t->alloc;
    for (usize i = 0; i < t->entries.count; i++)
    {
        if (t->entries.items[i].name.data != NULL)
        {
            mel_dealloc(alloc, t->entries.items[i].name.data);
        }
    }
    mel_array_free(&t->entries);
    mel_dealloc(alloc, t->buckets);
    mel_dealloc(alloc, t);
}

Mel_Quark mel_quark_intern(Mel_Quark_Table* t, str8 s)
{
    if (t == NULL || str8_is_empty(s))
        return MEL_QUARK_NONE;

    u64  hash = str8_hash(s);
    bool found = false;
    u32  idx = mel__quark_probe(t, hash, s, &found);
    if (found)
        return t->buckets[idx].quark;

    if ((t->occupied + 1u) * 4u >= t->bucket_count * 3u)
    {
        if (!mel__quark_grow(t, t->bucket_count * 2u))
            return MEL_QUARK_NONE;
        idx = mel__quark_probe(t, hash, s, &found);
    }

    u8* buf = mel_alloc(t->alloc, (usize)s.len);
    if (buf == NULL)
        return MEL_QUARK_NONE;
    memcpy(buf, s.data, (usize)s.len);
    str8 owned = (str8){ .data = buf, .len = s.len };

    mel_array_push(&t->entries, ((Mel_Quark_Entry){ .name = owned }));
    Mel_Quark quark = (Mel_Quark)t->entries.count;

    t->buckets[idx].hash = hash;
    t->buckets[idx].quark = quark;
    t->occupied += 1u;

    return quark;
}

Mel_Quark mel_quark_lookup(const Mel_Quark_Table* t, str8 s)
{
    if (t == NULL || str8_is_empty(s))
        return MEL_QUARK_NONE;

    u64  hash = str8_hash(s);
    bool found = false;
    u32  idx = mel__quark_probe(t, hash, s, &found);
    return found ? t->buckets[idx].quark : MEL_QUARK_NONE;
}

str8 mel_quark_get(const Mel_Quark_Table* t, Mel_Quark q)
{
    if (t == NULL || q == MEL_QUARK_NONE || (usize)q > t->entries.count)
    {
        return STR8_EMPTY;
    }
    return t->entries.items[q - 1u].name;
}

u32 mel_quark_count(const Mel_Quark_Table* t) { return t ? (u32)t->entries.count : 0u; }
