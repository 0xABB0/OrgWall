#include <allocator/allocator.h>
#include <collection.skiplist/skiplist.h>

static u64 mel__skiplist_xorshift64(u64* state)
{
    u64 x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

static u32 mel__skiplist_random_level(Mel_SkipList* sl)
{
    u32 lvl = 1;
    while (lvl < MEL_SKIPLIST_MAX_LEVEL && lvl <= sl->level &&
           (mel__skiplist_xorshift64(&sl->rng_state) & 1))
    {
        lvl++;
    }
    return lvl;
}

static Mel_SkipNode* mel__skiplist_create_node(const Mel_Alloc* alloc, u32 level, void* key, void* value)
{
    usize size = sizeof(Mel_SkipNode) + sizeof(Mel_SkipNode*) * level;
    Mel_SkipNode* node = (Mel_SkipNode*)mel_calloc(alloc, size);
    node->key = key;
    node->value = value;
    node->level = level;
    return node;
}

void mel_skiplist_init(Mel_SkipList* sl, Mel_SkipList_Cmp cmp, const Mel_Alloc* alloc)
{
    assert(sl != NULL);
    assert(cmp != NULL);
    assert(alloc != NULL);

    sl->cmp = cmp;
    sl->allocator = alloc;
    sl->count = 0;
    sl->level = 1;
    sl->rng_state = 0x123456789ABCDEF0ULL;
    sl->header = mel__skiplist_create_node(alloc, MEL_SKIPLIST_MAX_LEVEL, NULL, NULL);
}

void mel_skiplist_free(Mel_SkipList* sl)
{
    assert(sl != NULL);

    Mel_SkipNode* curr = sl->header->forward[0];
    while (curr)
    {
        Mel_SkipNode* next = curr->forward[0];
        mel_dealloc(sl->allocator, curr);
        curr = next;
    }
    mel_dealloc(sl->allocator, sl->header);
    sl->header = NULL;
    sl->count = 0;
    sl->level = 1;
}

bool mel_skiplist_insert(Mel_SkipList* sl, void* key, void* value)
{
    assert(sl != NULL);

    Mel_SkipNode* update[MEL_SKIPLIST_MAX_LEVEL] = {0};
    Mel_SkipNode* curr = sl->header;

    for (i32 i = (i32)sl->level - 1; i >= 0; i--)
    {
        while (curr->forward[i] && sl->cmp(curr->forward[i]->key, key) < 0)
        {
            curr = curr->forward[i];
        }
        update[i] = curr;
    }

    Mel_SkipNode* existing = curr->forward[0];
    if (existing && sl->cmp(existing->key, key) == 0)
    {
        return false;
    }

    u32 new_level = mel__skiplist_random_level(sl);
    if (new_level > sl->level)
    {
        for (u32 i = sl->level; i < new_level; i++)
        {
            update[i] = sl->header;
        }
        sl->level = new_level;
    }

    Mel_SkipNode* new_node = mel__skiplist_create_node(sl->allocator, new_level, key, value);

    for (u32 i = 0; i < new_level; i++)
    {
        new_node->forward[i] = update[i]->forward[i];
        update[i]->forward[i] = new_node;
    }

    sl->count++;
    return true;
}

Mel_SkipNode* mel_skiplist_find(Mel_SkipList* sl, void* key)
{
    assert(sl != NULL);

    Mel_SkipNode* curr = sl->header;
    for (i32 i = (i32)sl->level - 1; i >= 0; i--)
    {
        while (curr->forward[i] && sl->cmp(curr->forward[i]->key, key) < 0)
        {
            curr = curr->forward[i];
        }
    }

    Mel_SkipNode* candidate = curr->forward[0];
    if (candidate && sl->cmp(candidate->key, key) == 0)
    {
        return candidate;
    }
    return NULL;
}

bool mel_skiplist_remove(Mel_SkipList* sl, void* key)
{
    assert(sl != NULL);

    Mel_SkipNode* update[MEL_SKIPLIST_MAX_LEVEL] = {0};
    Mel_SkipNode* curr = sl->header;

    for (i32 i = (i32)sl->level - 1; i >= 0; i--)
    {
        while (curr->forward[i] && sl->cmp(curr->forward[i]->key, key) < 0)
        {
            curr = curr->forward[i];
        }
        update[i] = curr;
    }

    Mel_SkipNode* target = curr->forward[0];
    if (!target || sl->cmp(target->key, key) != 0)
    {
        return false;
    }

    for (u32 i = 0; i < sl->level; i++)
    {
        if (update[i]->forward[i] != target)
        {
            break;
        }
        update[i]->forward[i] = target->forward[i];
    }

    mel_dealloc(sl->allocator, target);
    sl->count--;

    while (sl->level > 1 && sl->header->forward[sl->level - 1] == NULL)
    {
        sl->level--;
    }

    return true;
}

Mel_SkipNode* mel_skiplist_min(Mel_SkipList* sl)
{
    assert(sl != NULL);
    return sl->header->forward[0];
}

Mel_SkipNode* mel_skiplist_max(Mel_SkipList* sl)
{
    assert(sl != NULL);

    Mel_SkipNode* curr = sl->header;
    for (i32 i = (i32)sl->level - 1; i >= 0; i--)
    {
        while (curr->forward[i])
        {
            curr = curr->forward[i];
        }
    }

    if (curr == sl->header)
    {
        return NULL;
    }
    return curr;
}

usize mel_skiplist_count(Mel_SkipList* sl)
{
    assert(sl != NULL);
    return sl->count;
}

bool mel_skiplist_empty(Mel_SkipList* sl)
{
    assert(sl != NULL);
    return sl->count == 0;
}

void mel_skiplist_clear(Mel_SkipList* sl)
{
    assert(sl != NULL);

    Mel_SkipNode* curr = sl->header->forward[0];
    while (curr)
    {
        Mel_SkipNode* next = curr->forward[0];
        mel_dealloc(sl->allocator, curr);
        curr = next;
    }

    for (u32 i = 0; i < MEL_SKIPLIST_MAX_LEVEL; i++)
    {
        sl->header->forward[i] = NULL;
    }

    sl->count = 0;
    sl->level = 1;
}

bool mel_skiplist_contains(Mel_SkipList* sl, void* key)
{
    return mel_skiplist_find(sl, key) != NULL;
}

void mel_skiplist_seed(Mel_SkipList* sl, u64 seed)
{
    assert(sl != NULL);
    assert(seed != 0);
    sl->rng_state = seed;
}
