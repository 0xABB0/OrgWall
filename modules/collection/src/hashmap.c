#include <collection.map.hashmap/hashmap.h>
#include <allocator/allocator.h>
#include <hash/xxh.h>

#include <string.h>

void mel_hashmap_init(Mel_HashMap* hm, Mel_HashMap_Hash hash_fn, Mel_HashMap_Eq eq_fn, const Mel_Alloc* alloc)
{
    hm->entries = nullptr;
    hm->count = 0;
    hm->capacity = 0;
    hm->hash = hash_fn;
    hm->eq = eq_fn;
    hm->allocator = alloc;
}

void mel_hashmap_free(Mel_HashMap* hm)
{
    if (hm->entries != nullptr)
    {
        mel_dealloc(hm->allocator, hm->entries);
        hm->entries = nullptr;
    }
    hm->count = 0;
    hm->capacity = 0;
}

static usize mel__hashmap_next_pow2(usize v)
{
    if (v == 0) return MEL_HASHMAP_INIT_CAP;
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v |= v >> 32;
    v++;
    return v;
}

static void mel__hashmap_insert_entry(Mel_HashMapEntry* entries, usize capacity,
                                       Mel_HashMap_Hash hash_fn, Mel_HashMap_Eq eq_fn,
                                       void* key, void* value)
{
    u64 h = hash_fn(key);
    usize idx = h & (capacity - 1);

    for (;;)
    {
        if (entries[idx].state != MEL_HASHMAP_OCCUPIED)
        {
            entries[idx].key = key;
            entries[idx].value = value;
            entries[idx].state = MEL_HASHMAP_OCCUPIED;
            return;
        }
        if (eq_fn(entries[idx].key, key))
        {
            entries[idx].value = value;
            return;
        }
        idx = (idx + 1) & (capacity - 1);
    }
}

static void mel__hashmap_grow(Mel_HashMap* hm, usize new_cap)
{
    new_cap = mel__hashmap_next_pow2(new_cap);
    if (new_cap < MEL_HASHMAP_INIT_CAP) new_cap = MEL_HASHMAP_INIT_CAP;

    Mel_HashMapEntry* new_entries = mel_calloc(hm->allocator, sizeof(Mel_HashMapEntry) * new_cap);

    if (hm->entries != nullptr)
    {
        for (usize i = 0; i < hm->capacity; i++)
        {
            if (hm->entries[i].state == MEL_HASHMAP_OCCUPIED)
            {
                mel__hashmap_insert_entry(new_entries, new_cap, hm->hash, hm->eq,
                                           hm->entries[i].key, hm->entries[i].value);
            }
        }
        mel_dealloc(hm->allocator, hm->entries);
    }

    hm->entries = new_entries;
    hm->capacity = new_cap;
}

bool mel_hashmap_put(Mel_HashMap* hm, void* key, void* value)
{
    if (hm->entries == nullptr || (hm->count + 1) > (usize)((f64)hm->capacity * MEL_HASHMAP_MAX_LOAD))
    {
        usize new_cap = hm->capacity == 0 ? MEL_HASHMAP_INIT_CAP : hm->capacity * 2;
        mel__hashmap_grow(hm, new_cap);
    }

    u64 h = hm->hash(key);
    usize idx = h & (hm->capacity - 1);

    for (;;)
    {
        if (hm->entries[idx].state != MEL_HASHMAP_OCCUPIED)
        {
            hm->entries[idx].key = key;
            hm->entries[idx].value = value;
            hm->entries[idx].state = MEL_HASHMAP_OCCUPIED;
            hm->count++;
            return true;
        }
        if (hm->eq(hm->entries[idx].key, key))
        {
            hm->entries[idx].value = value;
            return false;
        }
        idx = (idx + 1) & (hm->capacity - 1);
    }
}

void* mel_hashmap_get(Mel_HashMap* hm, void* key)
{
    if (hm->entries == nullptr) return nullptr;

    u64 h = hm->hash(key);
    usize idx = h & (hm->capacity - 1);

    for (;;)
    {
        if (hm->entries[idx].state != MEL_HASHMAP_OCCUPIED)
        {
            return nullptr;
        }
        if (hm->eq(hm->entries[idx].key, key))
        {
            return hm->entries[idx].value;
        }
        idx = (idx + 1) & (hm->capacity - 1);
    }
}

bool mel_hashmap_contains(Mel_HashMap* hm, void* key)
{
    if (hm->entries == nullptr) return false;

    u64 h = hm->hash(key);
    usize idx = h & (hm->capacity - 1);

    for (;;)
    {
        if (hm->entries[idx].state != MEL_HASHMAP_OCCUPIED)
        {
            return false;
        }
        if (hm->eq(hm->entries[idx].key, key))
        {
            return true;
        }
        idx = (idx + 1) & (hm->capacity - 1);
    }
}

bool mel_hashmap_remove(Mel_HashMap* hm, void* key)
{
    if (hm->entries == nullptr) return false;

    u64 h = hm->hash(key);
    usize idx = h & (hm->capacity - 1);

    for (;;)
    {
        if (hm->entries[idx].state != MEL_HASHMAP_OCCUPIED)
        {
            return false;
        }
        if (hm->eq(hm->entries[idx].key, key))
        {
            break;
        }
        idx = (idx + 1) & (hm->capacity - 1);
    }

    hm->entries[idx].state = MEL_HASHMAP_EMPTY;
    hm->entries[idx].key = nullptr;
    hm->entries[idx].value = nullptr;
    hm->count--;

    usize next = (idx + 1) & (hm->capacity - 1);
    while (hm->entries[next].state == MEL_HASHMAP_OCCUPIED)
    {
        usize ideal = hm->hash(hm->entries[next].key) & (hm->capacity - 1);

        bool should_shift;
        if (next >= idx)
        {
            should_shift = (ideal <= idx) || (ideal > next);
        }
        else
        {
            should_shift = (ideal <= idx) && (ideal > next);
        }

        if (should_shift)
        {
            hm->entries[idx] = hm->entries[next];
            hm->entries[next].state = MEL_HASHMAP_EMPTY;
            hm->entries[next].key = nullptr;
            hm->entries[next].value = nullptr;
            idx = next;
        }
        next = (next + 1) & (hm->capacity - 1);
    }

    return true;
}

usize mel_hashmap_count(Mel_HashMap* hm)
{
    return hm->count;
}

bool mel_hashmap_empty(Mel_HashMap* hm)
{
    return hm->count == 0;
}

void mel_hashmap_clear(Mel_HashMap* hm)
{
    if (hm->entries != nullptr)
    {
        memset(hm->entries, 0, sizeof(Mel_HashMapEntry) * hm->capacity);
    }
    hm->count = 0;
}

void mel_hashmap_reserve(Mel_HashMap* hm, usize capacity)
{
    usize needed = mel__hashmap_next_pow2((usize)((f64)capacity / MEL_HASHMAP_MAX_LOAD) + 1);
    if (needed > hm->capacity)
    {
        mel__hashmap_grow(hm, needed);
    }
}

u64 mel_hashmap_hash_u64(const void* key)
{
    u64 val = (u64)(usize)key;
    return mel_xxh64(&val, sizeof(val), 0);
}

u64 mel_hashmap_hash_str(const void* key)
{
    const char* str = (const char*)key;
    return mel_xxh64(str, strlen(str), 0);
}

u64 mel_hashmap_hash_ptr(const void* key)
{
    usize val = (usize)key;
    return mel_xxh64(&val, sizeof(val), 0);
}

bool mel_hashmap_eq_u64(const void* a, const void* b)
{
    return (u64)(usize)a == (u64)(usize)b;
}

bool mel_hashmap_eq_str(const void* a, const void* b)
{
    return strcmp((const char*)a, (const char*)b) == 0;
}
