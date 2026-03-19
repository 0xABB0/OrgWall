#pragma once

#include "gpu.pipeline_cache.fwd.h"
#include "gpu.pipeline.h"
#include "collection.hashmap.h"
#include "allocator.fwd.h"

typedef u64 Mel_Gpu_Pipeline_State;

#define MEL_PSO_BLEND_SHIFT       0
#define MEL_PSO_BLEND_MASK        0xFu
#define MEL_PSO_CULL_SHIFT        4
#define MEL_PSO_CULL_MASK         0x3u
#define MEL_PSO_TOPOLOGY_SHIFT    6
#define MEL_PSO_TOPOLOGY_MASK     0x7u
#define MEL_PSO_DEPTH_TEST_SHIFT  9
#define MEL_PSO_DEPTH_WRITE_SHIFT 10
#define MEL_PSO_DEPTH_CMP_SHIFT   11
#define MEL_PSO_DEPTH_CMP_MASK    0x7u
#define MEL_PSO_PIPE_TYPE_SHIFT   14
#define MEL_PSO_PIPE_TYPE_MASK    0x3u
#define MEL_PSO_DYN_CULL_SHIFT    16
#define MEL_PSO_USE_TEX_SHIFT     17

typedef struct {
    u64 vertex_in_hash;
    u64 shader_hash;
    u64 fragment_out_hash;
} Mel_Gpu_Pipeline_Cache_Key;

typedef struct {
    Mel_Gpu_Pipeline* pipeline;
    Mel_Gpu_Pipeline_Cache_Key* key;
} Mel_Gpu_Pipeline_Cache_Entry;

struct Mel_Gpu_Pipeline_Cache {
    Mel_HashMap map;
    Mel_Gpu_Pipeline_Cache_Entry* entries;
    u32 entry_count;
    u32 entry_capacity;
    const Mel_Alloc* alloc;
};

void mel_gpu_pipeline_cache_init(Mel_Gpu_Pipeline_Cache* cache, const Mel_Alloc* alloc);
void mel_gpu_pipeline_cache_shutdown(Mel_Gpu_Pipeline_Cache* cache, Mel_Gpu_Device* dev);

Mel_Gpu_Pipeline* mel_gpu_pipeline_cache_get_opt(
    Mel_Gpu_Pipeline_Cache* cache,
    Mel_Gpu_Device* dev,
    Mel_Gpu_Pipeline_Opt opt);

#define mel_gpu_pipeline_cache_get(cache, dev, ...) \
    mel_gpu_pipeline_cache_get_opt((cache), (dev), (Mel_Gpu_Pipeline_Opt){__VA_ARGS__})

Mel_Gpu_Pipeline_State mel_gpu_pipeline_state_pack(Mel_Gpu_Pipeline_Opt* opt);
Mel_Gpu_Pipeline_Cache_Key mel_gpu_pipeline_cache_key(Mel_Gpu_Pipeline_Opt* opt);
