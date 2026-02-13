#pragma once

#include "gpu.buffer.h"
#include "gpu.pipeline.fwd.h"
#include "gpu.texture.fwd.h"
#include "allocator.fwd.h"
#include "math.vec4.h"
#include "math.mat4.h"

typedef struct
{
    f32 x, y;
    f32 u, v;
    f32 r, g, b, a;
} Mel_SpriteVertex;

typedef struct
{
    VkDescriptorSet descriptor;
    u32 index_offset;
    u32 index_count;
} Mel_SpriteBatch_Draw;

typedef struct Mel_SpriteBatch
{
    Mel_Gpu_Buffer vertex_buffer;
    Mel_Gpu_Buffer index_buffer;

    Mel_SpriteVertex* vertices;
    u16* indices;
    u32 vertex_count;
    u32 index_count;
    u32 max_sprites;

    Mel_Gpu_Pipeline* pipeline;
    VkDescriptorSet current_descriptor;
    Mel_Gpu_Texture* current_texture;

    Mel_SpriteBatch_Draw* draws;
    u32 draw_count;
    u32 draw_capacity;
    const Mel_Alloc* allocator;
} Mel_SpriteBatch;

typedef struct
{
    u32 max_sprites;
    const Mel_Alloc* allocator;
} Mel_SpriteBatch_Opt;

bool mel_sprite_batch_init_opt(Mel_SpriteBatch* batch, Mel_Gpu_Device* dev, Mel_SpriteBatch_Opt opt);
#define mel_sprite_batch_init(batch, dev, ...) mel_sprite_batch_init_opt((batch), (dev), (Mel_SpriteBatch_Opt){__VA_ARGS__})

void mel_sprite_batch_shutdown(Mel_SpriteBatch* batch, Mel_Gpu_Device* dev);

void mel_sprite_batch_begin(Mel_SpriteBatch* batch, Mel_Gpu_Pipeline* pipeline);
void mel_sprite_batch_end(Mel_SpriteBatch* batch, Mel_Gpu_Device* dev, VkCommandBuffer cmd, Mel_Mat4* projection);

void mel_sprite_batch_set_texture(Mel_SpriteBatch* batch, Mel_Gpu_Texture* texture);

void mel_sprite_batch_draw(Mel_SpriteBatch* batch, f32 x, f32 y, f32 w, f32 h, Mel_Vec4 color);
void mel_sprite_batch_draw_uv(Mel_SpriteBatch* batch, f32 x, f32 y, f32 w, f32 h, f32 u0, f32 v0, f32 u1, f32 v1, Mel_Vec4 color);
