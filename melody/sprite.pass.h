#pragma once

#include "sprite.pass.fwd.h"
#include "core.types.h"
#include "math.vec2.h"
#include "math.vec4.h"
#include "math.mat4.h"
#include "math.geo.rect.h"
#include "texture.pool.fwd.h"
#include "gpu.shader.h"
#include "gpu.pipeline.h"
#include "gpu.texture.h"
#include "gpu.buffer.h"
#include "gpu.cmd.h"
#include "render.list.fwd.h"
#include "render.pass.fwd.h"
#include "allocator.fwd.h"

struct Mel_Sprite_Entry {
    Mel_Vec2 pos;
    Mel_Vec2 size;
    Mel_Rect uv;
    Mel_Vec4 color;
    Mel_Texture_Handle tex;
};

[[nodiscard]] static inline u64 mel_sort_key_sprite(u8 layer, f32 depth, u16 material, u16 texture_bucket)
{
    u32 bits;
    __builtin_memcpy(&bits, &depth, sizeof(u32));
    u32 mask = (bits >> 31) ? 0xFFFFFFFF : 0x80000000;
    bits ^= mask;
    u32 depth24 = bits >> 8;
    return ((u64)layer << 56) | ((u64)depth24 << 32) | ((u64)material << 16) | (u64)texture_bucket;
}

[[nodiscard]] static inline u64 mel_sort_key_opaque(f32 depth)
{
    u32 bits;
    __builtin_memcpy(&bits, &depth, sizeof(u32));
    u32 mask = (bits >> 31) ? 0xFFFFFFFF : 0x80000000;
    bits ^= mask;
    return (u64)bits;
}

[[nodiscard]] static inline u64 mel_sort_key_transparent(f32 depth)
{
    return ~mel_sort_key_opaque(depth);
}

typedef struct {
    VkDescriptorSet descriptor;
    u32 index_offset;
    u32 index_count;
} Mel_Sprite_Draw_Cmd;

#ifndef MEL_MAX_FRAMES_IN_FLIGHT
#define MEL_MAX_FRAMES_IN_FLIGHT 3
#endif

typedef struct {
    Mel_Gpu_Buffer vertex_buffer;
    Mel_Gpu_Buffer index_buffer;
} Mel_Sprite_Gpu_Frame;

typedef struct Mel_Texture_Pool Mel_Texture_Pool;

struct Mel_Sprite_Pass {
    Mel_Gpu_Shader shader;
    Mel_Gpu_Pipeline pipeline;
    Mel_Gpu_Texture white_texture;
    Mel_Gpu_Device* dev;
    Mel_Texture_Pool* pool;

    Mel_Sprite_Gpu_Frame gpu_frames[MEL_MAX_FRAMES_IN_FLIGHT];
    u32 gpu_frame_index;
    void* vertices;
    u16* indices;
    u32 vertex_count;
    u32 index_count;
    u32 max_sprites;

    Mel_Sprite_Draw_Cmd* draws;
    u32 draw_count;
    u32 draw_capacity;
    VkDescriptorSet current_descriptor;
    Mel_Gpu_Texture* current_texture;
    const Mel_Alloc* alloc;
};

typedef struct {
    Mel_Gpu_Device* dev;
    VkFormat color_format;
    u32 max_sprites;
    const Mel_Alloc* alloc;
} Mel_Sprite_Pass_Init_Opt;

bool mel_sprite_pass_init_opt(Mel_Sprite_Pass* sp, Mel_Sprite_Pass_Init_Opt opt);
#define mel_sprite_pass_init(sp, ...) mel_sprite_pass_init_opt((sp), (Mel_Sprite_Pass_Init_Opt){__VA_ARGS__})

void mel_sprite_pass_shutdown(Mel_Sprite_Pass* sp);

void mel_sprite_pass_execute(Mel_Render_Pass_Ctx* ctx);

void mel_sprite_pass_draw(Mel_Render_List* list, Mel_Sprite_Pass* sp);

typedef struct {
    Mel_Vec2 pos;
    Mel_Vec2 size;
    Mel_Vec4 color;
    Mel_Texture_Handle tex;
    Mel_Rect uv;
    u8 layer;
    f32 depth;
} Mel_Draw_Sprite_Opt;

void mel_draw_sprite_opt(Mel_Render_List* list, Mel_Draw_Sprite_Opt opt);
#define mel_draw_sprite(list, ...) mel_draw_sprite_opt((list), (Mel_Draw_Sprite_Opt){__VA_ARGS__})
