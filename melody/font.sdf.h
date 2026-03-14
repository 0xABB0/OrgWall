#pragma once

#include "core.types.h"
#include "allocator.fwd.h"
#include "string.str8.fwd.h"
#include "font.atlas.fwd.h"
#include "font.descriptor.h"
#include "gpu.texture.h"
#include "gpu.device.fwd.h"
#include "collection.slotmap.h"
#include "collection.hashmap.h"
#include "math.vec2.h"
// ASYNC_V2: VFS removed
typedef struct Mel_Vfs Mel_Vfs;

typedef struct Mel_Font_SDF_Entry Mel_Font_SDF_Entry;
typedef struct Mel_Font_SDF_Pool Mel_Font_SDF_Pool;

struct Mel_Font_SDF_Entry {
    Mel_Font_Descriptor desc;
    Mel_Gpu_Texture texture;
    u32 atlas_width, atlas_height;
    f32 px_range;
};

struct Mel_Font_SDF_Pool {
    Mel_SlotMap slotmap;
    Mel_HashMap path_to_handle;
    Mel_Gpu_Device* dev;
    Mel_Vfs* vfs;
    const Mel_Alloc* alloc;
};

typedef struct {
    str8 path;
    f32 size;
    u32 atlas_width;
    u32 atlas_height;
    u32 padding;
    f32 px_range;
} Mel_Font_SDF_Load_Opt;

void mel_font_sdf_pool_init(Mel_Font_SDF_Pool* pool, const Mel_Alloc* alloc, Mel_Gpu_Device* dev, Mel_Vfs* vfs);
void mel_font_sdf_pool_shutdown(Mel_Font_SDF_Pool* pool);
Mel_Font_Handle mel_font_sdf_pool_load_opt(Mel_Font_SDF_Pool* pool, Mel_Font_SDF_Load_Opt opt);
#define mel_font_sdf_pool_load(pool, ...) mel_font_sdf_pool_load_opt((pool), (Mel_Font_SDF_Load_Opt){__VA_ARGS__})
Mel_Font_SDF_Entry* mel_font_sdf_pool_get(Mel_Font_SDF_Pool* pool, Mel_Font_Handle handle);
Mel_Gpu_Texture* mel_font_sdf_pool_get_texture(Mel_Font_SDF_Pool* pool, Mel_Font_Handle handle);
Mel_Vec2 mel_font_sdf_measure_text(Mel_Font_SDF_Pool* pool, Mel_Font_Handle handle, str8 text);
