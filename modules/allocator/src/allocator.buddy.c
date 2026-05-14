#include <allocator.buddy/allocator.buddy.h>
#include <string.h>
#include <stdlib.h>

static i32 mel__buddy_log2(usize n)
{
    i32 r = 0;
    while (n > 1) { n >>= 1; r++; }
    return r;
}

static usize mel__buddy_next_pow2(usize n)
{
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    n |= n >> 32;
    n++;
    return n;
}

void mel_buddy_init_opt(Mel_Buddy_Alloc* buddy, void* buffer, usize size, Mel_Buddy_Init_Opt opt)
{
    assert(buddy != NULL);
    assert(buffer != NULL);
    assert(size > 0);
    assert((size & (size - 1)) == 0);

    usize min_block = opt.min_block_size > 0 ? opt.min_block_size : 64;
    assert((min_block & (min_block - 1)) == 0);
    assert(min_block <= size);

    buddy->base = (u8*)buffer;
    buddy->size = size;
    buddy->min_block = min_block;
    buddy->levels = mel__buddy_log2(size / min_block) + 1;
    buddy->tree_size = (1u << buddy->levels) - 1;

    if (opt.tree_buffer)
    {
        buddy->tree = opt.tree_buffer;
    }
    else
    {
        buddy->tree = (u8*)malloc(buddy->tree_size);
    }

    memset(buddy->tree, MEL_BUDDY_FREE, buddy->tree_size);

#if MEL_ALLOCATOR_BUDDY_DEBUG
    buddy->alloc_count = 0;
    buddy->free_count = 0;
    buddy->current_usage = 0;
    buddy->peak_usage = 0;
    buddy->name = NULL;
#endif
}

static void* mel__buddy_alloc_recursive(Mel_Buddy_Alloc* buddy, usize node, i32 level, usize required_level)
{
    if (level == (i32)required_level)
    {
        if (buddy->tree[node - 1] != MEL_BUDDY_FREE) return NULL;
        buddy->tree[node - 1] = MEL_BUDDY_USED;

        usize block_size = buddy->size >> level;
        usize nodes_at_level = 1u << level;
        usize index_in_level = node - nodes_at_level;
        return buddy->base + index_in_level * block_size;
    }

    if (buddy->tree[node - 1] == MEL_BUDDY_USED) return NULL;

    void* ptr = mel__buddy_alloc_recursive(buddy, node * 2, level + 1, required_level);
    if (ptr)
    {
        buddy->tree[node - 1] = MEL_BUDDY_SPLIT;
        return ptr;
    }

    ptr = mel__buddy_alloc_recursive(buddy, node * 2 + 1, level + 1, required_level);
    if (ptr)
    {
        buddy->tree[node - 1] = MEL_BUDDY_SPLIT;
        return ptr;
    }

    return NULL;
}

void* mel_buddy_alloc(Mel_Buddy_Alloc* buddy, usize size)
{
    assert(buddy != NULL);
    assert(size > 0);

    usize actual = size < buddy->min_block ? buddy->min_block : mel__buddy_next_pow2(size);

    if (actual > buddy->size) return NULL;

    i32 target_level = mel__buddy_log2(buddy->size / actual);

    void* ptr = mel__buddy_alloc_recursive(buddy, 1, 0, target_level);

#if MEL_ALLOCATOR_BUDDY_DEBUG
    if (ptr)
    {
        buddy->alloc_count++;
        buddy->current_usage += actual;
        if (buddy->current_usage > buddy->peak_usage)
            buddy->peak_usage = buddy->current_usage;
    }
#endif

    return ptr;
}

static void mel__buddy_free_recursive(Mel_Buddy_Alloc* buddy, usize node, i32 level, void* ptr, i32 target_level)
{
    usize block_size = buddy->size >> level;
    usize nodes_at_level = 1u << level;
    usize index_in_level = node - nodes_at_level;
    u8* block_start = buddy->base + index_in_level * block_size;

    if (level == target_level)
    {
        assert(buddy->tree[node - 1] == MEL_BUDDY_USED);
        assert((u8*)ptr == block_start);
        buddy->tree[node - 1] = MEL_BUDDY_FREE;
        return;
    }

    assert(buddy->tree[node - 1] == MEL_BUDDY_SPLIT);

    u8* mid = block_start + block_size / 2;
    if ((u8*)ptr < mid)
        mel__buddy_free_recursive(buddy, node * 2, level + 1, ptr, target_level);
    else
        mel__buddy_free_recursive(buddy, node * 2 + 1, level + 1, ptr, target_level);

    if (buddy->tree[node * 2 - 1] == MEL_BUDDY_FREE && buddy->tree[node * 2] == MEL_BUDDY_FREE)
        buddy->tree[node - 1] = MEL_BUDDY_FREE;
}

void mel_buddy_free(Mel_Buddy_Alloc* buddy, void* ptr)
{
    assert(buddy != NULL);
    assert(ptr != NULL);
    assert((u8*)ptr >= buddy->base && (u8*)ptr < buddy->base + buddy->size);

    usize offset = (u8*)ptr - buddy->base;

    i32 level = buddy->levels - 1;
    while (level >= 0)
    {
        usize block_size = buddy->size >> level;
        if ((offset & (block_size - 1)) == 0)
        {
            usize nodes_at_level = 1u << level;
            usize index_in_level = offset / block_size;
            usize node = nodes_at_level + index_in_level;

            if (buddy->tree[node - 1] == MEL_BUDDY_USED)
            {
                mel__buddy_free_recursive(buddy, 1, 0, ptr, level);
#if MEL_ALLOCATOR_BUDDY_DEBUG
                buddy->free_count++;
                buddy->current_usage -= block_size;
#endif
                return;
            }
        }
        level--;
    }

    assert(false);
}

void mel_buddy_reset(Mel_Buddy_Alloc* buddy)
{
    assert(buddy != NULL);
    memset(buddy->tree, MEL_BUDDY_FREE, buddy->tree_size);
#if MEL_ALLOCATOR_BUDDY_DEBUG
    buddy->current_usage = 0;
#endif
}
