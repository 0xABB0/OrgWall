#pragma once
#include <core/types.h>
#include "cfg.h"
#include <collection.pool/collection.pool.h>

typedef struct Mel_Slab_Class {
    Mel_Pool pool;
    usize block_size;
} Mel_Slab_Class;

typedef struct Mel_Slab_Alloc {
    Mel_Slab_Class* classes;
    i32 class_count;
#if MEL_COLLECTION_SLAB_DEBUG
    _Atomic(usize) alloc_count;
    _Atomic(usize) free_count;
    const char* name;
#endif
} Mel_Slab_Alloc;

typedef struct {
    void* buffer;
    usize buffer_size;
    usize block_size;
} Mel_Slab_Class_Desc;

void  mel_slab_init(Mel_Slab_Alloc* slab, Mel_Slab_Class* class_storage, Mel_Slab_Class_Desc* classes, i32 class_count);
void* mel_slab_alloc(Mel_Slab_Alloc* slab, usize size);
void  mel_slab_free(Mel_Slab_Alloc* slab, void* ptr);
void  mel_slab_reset(Mel_Slab_Alloc* slab);

#include "collection.slab.inl"
