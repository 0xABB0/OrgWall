#pragma once

#include "gpu.geometry_pool.fwd.h"
#include "gpu.buffer.h"
#include "gpu.device.fwd.h"
#include "collection.slotmap.h"
#include "collection.bitset.h"
#include "collection.array.h"
#include "allocator.fwd.h"

#define MEL_GEOMETRY_STORAGE_CPU      0
#define MEL_GEOMETRY_STORAGE_UNIFIED  1
#define MEL_GEOMETRY_STORAGE_DISCRETE 2

typedef struct {
    u32 vertex_offset;
    u32 vertex_count;
    u32 index_offset;
    u32 index_count;
    u32 meshlet_offset;
    u32 meshlet_count;
    u32 index_type;
    u32 _pad;
} Mel_Geometry_Region;

_Static_assert(sizeof(Mel_Geometry_Region) == 32, "Mel_Geometry_Region must be 32 bytes for GPU cache alignment");

typedef struct {
    u64 offset;
    u64 size;
} Mel_Geometry_Free_Region;

typedef Mel_Array(Mel_Geometry_Free_Region) Mel_Geometry_Free_List;

typedef struct {
    Mel_Gpu_Buffer gpu;
    u8* cpu;
    u64 cursor;
    u64 capacity;
    u32 stride;
    Mel_Geometry_Free_List free_list;
} Mel_Geometry_Lane;

struct Mel_Geometry_Pool {
    Mel_SlotMap catalog;
    Mel_Gpu_Buffer catalog_gpu;
    Mel_BitSet catalog_dirty;

    u32 storage_mode;

    Mel_Geometry_Lane vertices;
    Mel_Geometry_Lane indices_u16;
    Mel_Geometry_Lane indices_u32;
    Mel_Geometry_Lane meshlet_descs;
    Mel_Geometry_Lane meshlet_data;

    Mel_Gpu_Device* dev;
    const Mel_Alloc* alloc;
};

typedef struct {
    const void* vertices;
    u32 vertex_count;
    const void* indices;
    u32 index_count;
    u32 index_type;
    const void* meshlet_descs;
    u32 meshlet_count;
    const void* meshlet_data;
    u32 meshlet_data_size;
} Mel_Geometry_Upload;

typedef struct {
    Mel_Gpu_Device* dev;
    const Mel_Alloc* alloc;
    u32 vertex_stride;
    u64 vertex_capacity;
    u64 index_capacity;
    bool cpu_readable;
} Mel_Geometry_Pool_Opt;

void mel_geometry_pool_init_opt(Mel_Geometry_Pool* pool, Mel_Geometry_Pool_Opt opt);
#define mel_geometry_pool_init(pool, ...) \
    mel_geometry_pool_init_opt((pool), (Mel_Geometry_Pool_Opt){__VA_ARGS__})

void mel_geometry_pool_shutdown(Mel_Geometry_Pool* pool);

Mel_Geometry_Handle mel_geometry_pool_upload(Mel_Geometry_Pool* pool,
                                             const Mel_Geometry_Upload* upload);
void mel_geometry_pool_remove(Mel_Geometry_Pool* pool, Mel_Geometry_Handle h);

Mel_Geometry_Region mel_geometry_pool_region(Mel_Geometry_Pool* pool, Mel_Geometry_Handle h);

void mel_geometry_pool_upload_catalog(Mel_Geometry_Pool* pool);

Mel_Gpu_Buffer* mel_geometry_pool_catalog_buffer(Mel_Geometry_Pool* pool);
Mel_Gpu_Buffer* mel_geometry_pool_vertex_buffer(Mel_Geometry_Pool* pool);
Mel_Gpu_Buffer* mel_geometry_pool_index_buffer_u16(Mel_Geometry_Pool* pool);
Mel_Gpu_Buffer* mel_geometry_pool_index_buffer_u32(Mel_Geometry_Pool* pool);
Mel_Gpu_Buffer* mel_geometry_pool_meshlet_desc_buffer(Mel_Geometry_Pool* pool);
Mel_Gpu_Buffer* mel_geometry_pool_meshlet_data_buffer(Mel_Geometry_Pool* pool);
u32             mel_geometry_pool_vertex_stride(Mel_Geometry_Pool* pool);

const void* mel_geometry_pool_cpu_vertices(Mel_Geometry_Pool* pool, Mel_Geometry_Handle h);
const void* mel_geometry_pool_cpu_indices(Mel_Geometry_Pool* pool, Mel_Geometry_Handle h);
