#pragma once

#include "types.h"
#include "allocator.fwd.h"
#include "string.str8.fwd.h"
#include "texture.pool.fwd.h"
#include "gpu.texture.h"
#include "gpu.device.fwd.h"
#include "gpu.pipeline.fwd.h"
#include "collection.slotmap.h"
#include "collection.hashmap.h"
#include "async.job.fwd.h"

#define MEL_TEXTURE_STATE_UNLOADED 0
#define MEL_TEXTURE_STATE_LOADING  1
#define MEL_TEXTURE_STATE_LOADED   2
#define MEL_TEXTURE_STATE_FAILED   3

typedef struct {
    Mel_Gpu_Texture gpu_texture;
    u64 path_hash;
    u32 state;
} Mel_Texture_Entry;

typedef struct Mel_Assets Mel_Assets;

struct Mel_Texture_Pool {
    Mel_SlotMap slotmap;
    Mel_HashMap path_to_handle;
    Mel_Gpu_Device* dev;
    Mel_Gpu_Pipeline* pipeline;
    Mel_Gpu_Texture fallback;
    Mel_Job_Context* job_ctx;
    const Mel_Alloc* alloc;
    Mel_Assets* assets;
};

typedef struct {
    Mel_Gpu_Pipeline* pipeline;
    Mel_Job_Context* job_ctx;
    Mel_Assets* assets;
} Mel_Texture_Pool_Opt;

void              mel_texture_pool_init_opt(Mel_Texture_Pool* pool, const Mel_Alloc* alloc, Mel_Gpu_Device* dev, Mel_Texture_Pool_Opt opt);
#define mel_texture_pool_init(pool, alloc, dev, ...) mel_texture_pool_init_opt((pool), (alloc), (dev), (Mel_Texture_Pool_Opt){__VA_ARGS__})

void              mel_texture_pool_shutdown(Mel_Texture_Pool* pool);
Mel_Texture_Handle mel_texture_pool_load(Mel_Texture_Pool* pool, str8 path);
Mel_Gpu_Texture*  mel_texture_pool_get(Mel_Texture_Pool* pool, Mel_Texture_Handle handle);
bool              mel_texture_pool_unload(Mel_Texture_Pool* pool, Mel_Texture_Handle handle);
bool              mel_texture_pool_is_loaded(Mel_Texture_Pool* pool, Mel_Texture_Handle handle);
u32               mel_texture_pool_state(Mel_Texture_Pool* pool, Mel_Texture_Handle handle);
u32               mel_texture_pool_count(Mel_Texture_Pool* pool);
void              mel_texture_pool_tick(Mel_Texture_Pool* pool);
