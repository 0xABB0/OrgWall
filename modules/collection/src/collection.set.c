#include <collection.set/set.h>
#include <collection.map.hashmap/hashmap.h>

void mel_set_init(Mel_Set* s, Mel_HashMap_Hash hash_fn, Mel_HashMap_Eq eq_fn, const Mel_Alloc* alloc)
{
    mel_hashmap_init(&s->map, hash_fn, eq_fn, alloc);
}

void mel_set_free(Mel_Set* s)
{
    mel_hashmap_free(&s->map);
}

bool mel_set_add(Mel_Set* s, void* key)
{
    return mel_hashmap_put(&s->map, key, nullptr);
}

bool mel_set_contains(Mel_Set* s, void* key)
{
    return mel_hashmap_contains(&s->map, key);
}

bool mel_set_remove(Mel_Set* s, void* key)
{
    return mel_hashmap_remove(&s->map, key);
}

usize mel_set_count(Mel_Set* s)
{
    return mel_hashmap_count(&s->map);
}

bool mel_set_empty(Mel_Set* s)
{
    return mel_hashmap_empty(&s->map);
}

void mel_set_clear(Mel_Set* s)
{
    mel_hashmap_clear(&s->map);
}

void mel_set_reserve(Mel_Set* s, usize capacity)
{
    mel_hashmap_reserve(&s->map, capacity);
}

void mel_set_union(Mel_Set* dst, Mel_Set* a, Mel_Set* b)
{
    for (usize i = 0; i < a->map.capacity; i++)
    {
        if (a->map.entries[i].state == MEL_HASHMAP_OCCUPIED)
        {
            mel_hashmap_put(&dst->map, a->map.entries[i].key, nullptr);
        }
    }
    for (usize i = 0; i < b->map.capacity; i++)
    {
        if (b->map.entries[i].state == MEL_HASHMAP_OCCUPIED)
        {
            mel_hashmap_put(&dst->map, b->map.entries[i].key, nullptr);
        }
    }
}

void mel_set_intersection(Mel_Set* dst, Mel_Set* a, Mel_Set* b)
{
    for (usize i = 0; i < a->map.capacity; i++)
    {
        if (a->map.entries[i].state == MEL_HASHMAP_OCCUPIED)
        {
            if (mel_hashmap_contains(&b->map, a->map.entries[i].key))
            {
                mel_hashmap_put(&dst->map, a->map.entries[i].key, nullptr);
            }
        }
    }
}

void mel_set_difference(Mel_Set* dst, Mel_Set* a, Mel_Set* b)
{
    for (usize i = 0; i < a->map.capacity; i++)
    {
        if (a->map.entries[i].state == MEL_HASHMAP_OCCUPIED)
        {
            if (!mel_hashmap_contains(&b->map, a->map.entries[i].key))
            {
                mel_hashmap_put(&dst->map, a->map.entries[i].key, nullptr);
            }
        }
    }
}

void mel_set_symmetric_difference(Mel_Set* dst, Mel_Set* a, Mel_Set* b)
{
    for (usize i = 0; i < a->map.capacity; i++)
    {
        if (a->map.entries[i].state == MEL_HASHMAP_OCCUPIED)
        {
            if (!mel_hashmap_contains(&b->map, a->map.entries[i].key))
            {
                mel_hashmap_put(&dst->map, a->map.entries[i].key, nullptr);
            }
        }
    }
    for (usize i = 0; i < b->map.capacity; i++)
    {
        if (b->map.entries[i].state == MEL_HASHMAP_OCCUPIED)
        {
            if (!mel_hashmap_contains(&a->map, b->map.entries[i].key))
            {
                mel_hashmap_put(&dst->map, b->map.entries[i].key, nullptr);
            }
        }
    }
}

bool mel_set_is_subset(Mel_Set* a, Mel_Set* b)
{
    if (mel_hashmap_count(&a->map) > mel_hashmap_count(&b->map))
    {
        return false;
    }
    for (usize i = 0; i < a->map.capacity; i++)
    {
        if (a->map.entries[i].state == MEL_HASHMAP_OCCUPIED)
        {
            if (!mel_hashmap_contains(&b->map, a->map.entries[i].key))
            {
                return false;
            }
        }
    }
    return true;
}

bool mel_set_is_superset(Mel_Set* a, Mel_Set* b)
{
    return mel_set_is_subset(b, a);
}

bool mel_set_equals(Mel_Set* a, Mel_Set* b)
{
    if (mel_hashmap_count(&a->map) != mel_hashmap_count(&b->map))
    {
        return false;
    }
    return mel_set_is_subset(a, b);
}
