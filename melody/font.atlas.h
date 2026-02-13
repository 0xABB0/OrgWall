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
#include "math.vec4.h"
#include "sprite.batch.fwd.h"

struct Mel_Font_Atlas_Entry {
    Mel_Font_Descriptor desc;
    Mel_Gpu_Texture atlas_texture;
    u32 atlas_width, atlas_height;
};

struct Mel_Font_Atlas_Pool {
    Mel_SlotMap slotmap;
    Mel_HashMap path_to_handle;
    Mel_Gpu_Device* dev;
    const Mel_Alloc* alloc;
};

typedef struct {
    str8 path;
    f32 size;
    u32 atlas_width;
    u32 atlas_height;
} Mel_Font_Atlas_Load_Opt;

void              mel_font_atlas_pool_init(Mel_Font_Atlas_Pool* pool, const Mel_Alloc* alloc, Mel_Gpu_Device* dev);
void              mel_font_atlas_pool_shutdown(Mel_Font_Atlas_Pool* pool);
Mel_Font_Handle   mel_font_atlas_pool_load_opt(Mel_Font_Atlas_Pool* pool, Mel_Font_Atlas_Load_Opt opt);
#define mel_font_atlas_pool_load(pool, ...) mel_font_atlas_pool_load_opt((pool), (Mel_Font_Atlas_Load_Opt){__VA_ARGS__})
Mel_Font_Atlas_Entry* mel_font_atlas_pool_get(Mel_Font_Atlas_Pool* pool, Mel_Font_Handle handle);

void mel_font_atlas_draw_text(Mel_Font_Atlas_Entry* entry, Mel_SpriteBatch* batch, str8 text, f32 x, f32 y, Mel_Vec4 color);
Mel_Vec2 mel_font_atlas_measure_text(Mel_Font_Atlas_Entry* entry, str8 text);
