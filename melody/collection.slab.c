#include "collection.slab.h"
#include <string.h>

void mel_slab_init(Mel_Slab_Alloc* slab, Mel_Slab_Class* class_storage, Mel_Slab_Class_Desc* classes, i32 class_count)
{
    assert(slab != NULL);
    assert(class_storage != NULL);
    assert(classes != NULL);
    assert(class_count > 0);

    slab->classes = class_storage;
    slab->class_count = class_count;

    for (i32 i = 0; i < class_count; i++)
    {
        slab->classes[i].block_size = classes[i].block_size;
        mel_pool_init(&slab->classes[i].pool, classes[i].buffer, classes[i].buffer_size, .block_size = classes[i].block_size);
    }

#if MEL_COLLECTION_SLAB_DEBUG
    atomic_store_explicit(&slab->alloc_count, 0, memory_order_relaxed);
    atomic_store_explicit(&slab->free_count, 0, memory_order_relaxed);
    slab->name = NULL;
#endif
}

void* mel_slab_alloc(Mel_Slab_Alloc* slab, usize size)
{
    assert(slab != NULL);
    assert(size > 0);

    for (i32 i = 0; i < slab->class_count; i++)
    {
        if (slab->classes[i].block_size >= size)
        {
#if MEL_COLLECTION_SLAB_DEBUG
            atomic_fetch_add_explicit(&slab->alloc_count, 1, memory_order_relaxed);
#endif
            return mel_pool_alloc(&slab->classes[i].pool);
        }
    }

    assert(false);
    return NULL;
}

void mel_slab_free(Mel_Slab_Alloc* slab, void* ptr)
{
    assert(slab != NULL);
    assert(ptr != NULL);

    for (i32 i = 0; i < slab->class_count; i++)
    {
        if (mel_pool_owns(&slab->classes[i].pool, ptr))
        {
#if MEL_COLLECTION_SLAB_DEBUG
            atomic_fetch_add_explicit(&slab->free_count, 1, memory_order_relaxed);
#endif
            mel_pool_free(&slab->classes[i].pool, ptr);
            return;
        }
    }

    assert(false);
}

void mel_slab_reset(Mel_Slab_Alloc* slab)
{
    assert(slab != NULL);
    for (i32 i = 0; i < slab->class_count; i++)
    {
        mel_pool_reset(&slab->classes[i].pool);
    }
}
