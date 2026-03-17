#pragma once

#include "gpu.storage_pool.fwd.h"
#include "gpu.buffer.h"
#include "collection.slotmap.h"
#include "collection.bitset.h"

struct Mel_Storage_Pool {
    Mel_SlotMap slots;
    Mel_BitSet dirty;
    Mel_Gpu_Buffer gpu_buffer;
    u8* mirror;
    usize item_size;
    u32 gpu_capacity;
    bool has_mirror;
    const Mel_Alloc* alloc;
};

typedef struct {
    Mel_Gpu_Device* dev;
    const Mel_Alloc* alloc;
    usize item_size;
    u32 initial_capacity;
    bool cpu_mirror;
    Mel_Gpu_Buffer_Usage extra_usage;
} Mel_Storage_Pool_Opt;

void mel_storage_pool_init_opt(Mel_Storage_Pool* pool, Mel_Storage_Pool_Opt opt);
#define mel_storage_pool_init(pool, ...) mel_storage_pool_init_opt((pool), (Mel_Storage_Pool_Opt){__VA_ARGS__})

void mel_storage_pool_shutdown(Mel_Storage_Pool* pool, Mel_Gpu_Device* dev);

Mel_Storage_Handle mel_storage_pool_alloc(Mel_Storage_Pool* pool, const void* data);
void               mel_storage_pool_free(Mel_Storage_Pool* pool, Mel_Storage_Handle handle);
void*              mel_storage_pool_get(Mel_Storage_Pool* pool, Mel_Storage_Handle handle);

void mel_storage_pool_set(Mel_Storage_Pool* pool, Mel_Storage_Handle handle, const void* data);
void mel_storage_pool_mark_dirty(Mel_Storage_Pool* pool, Mel_Storage_Handle handle);

bool mel_storage_pool_is_dirty(Mel_Storage_Pool* pool);
void mel_storage_pool_upload_dirty(Mel_Storage_Pool* pool, Mel_Gpu_Device* dev);
void mel_storage_pool_clear_dirty(Mel_Storage_Pool* pool);

u32              mel_storage_pool_count(Mel_Storage_Pool* pool);
Mel_Gpu_Buffer*  mel_storage_pool_buffer(Mel_Storage_Pool* pool);
