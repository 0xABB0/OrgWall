#pragma once

#include "core.types.h"
#include "gpu.buffer.h"
#include "gpu.pipeline.fwd.h"
#include "gpu.texture.fwd.h"
#include "allocator.fwd.h"
#include "math.vec2.h"
#include "math.vec4.h"
#include "math.mat4.h"
#include "font.atlas.fwd.h"
#include "texture.pool.fwd.h"
#include "string.str8.fwd.h"

typedef struct {
    f32 x, y;
    f32 u, v;
    f32 r, g, b, a;
} Mel_Draw_Vertex;

typedef struct {
    VkDescriptorSet descriptor;
    u32 index_offset;
    u32 index_count;
} Mel_Draw_Cmd;

#ifndef MEL_MAX_FRAMES_IN_FLIGHT
#define MEL_MAX_FRAMES_IN_FLIGHT 3
#endif

typedef struct {
    Mel_Gpu_Buffer vertex_buffer;
    Mel_Gpu_Buffer index_buffer;
    u32 vertex_capacity;
    u32 index_capacity;
} Mel_Draw_Gpu_Frame;

typedef struct Mel_Draw_Ctx Mel_Draw_Ctx;

struct Mel_Draw_Ctx {
    Mel_Draw_Vertex* vertices;
    u16* indices;
    u32 vertex_count;
    u32 index_count;
    u32 vertex_capacity;
    u32 index_capacity;

    Mel_Draw_Gpu_Frame gpu_frames[MEL_MAX_FRAMES_IN_FLIGHT];
    u32 gpu_frame_index;

    Mel_Draw_Cmd* draws;
    u32 draw_count;
    u32 draw_capacity;

    Mel_Gpu_Pipeline* pipeline;
    Mel_Gpu_Texture* default_texture;
    Mel_Gpu_Texture* current_texture;
    VkDescriptorSet current_descriptor;

    Mel_Texture_Pool* pool;
    Mel_Font_Atlas_Pool* font_pool;
    Mel_Gpu_Device* dev;
    const Mel_Alloc* alloc;

    bool committed;
};

typedef struct {
    Mel_Gpu_Pipeline* pipeline;
    Mel_Gpu_Texture* texture;
    Mel_Texture_Pool* pool;
    Mel_Font_Atlas_Pool* font_pool;
    Mel_Gpu_Device* dev;
    const Mel_Alloc* alloc;
} Mel_Draw_Ctx_Opt;

void mel_draw_ctx_init_opt(Mel_Draw_Ctx* ctx, Mel_Draw_Ctx_Opt opt);
#define mel_draw_ctx_init(ctx, ...) mel_draw_ctx_init_opt((ctx), (Mel_Draw_Ctx_Opt){__VA_ARGS__})

void mel_draw_ctx_shutdown(Mel_Draw_Ctx* ctx);

void mel_draw_ctx_clear(Mel_Draw_Ctx* ctx);
void mel_draw_ctx_rect(Mel_Draw_Ctx* ctx, f32 x, f32 y, f32 w, f32 h, Mel_Vec4 color);
void mel_draw_ctx_line(Mel_Draw_Ctx* ctx, Mel_Vec2 from, Mel_Vec2 to, f32 thickness, Mel_Vec4 color);

typedef struct {
    Mel_Font_Handle font;
    str8 text;
    f32 x;
    f32 y;
    Mel_Vec4 color;
} Mel_Draw_Ctx_Text_Opt;

void mel_draw_ctx_text_opt(Mel_Draw_Ctx* ctx, Mel_Draw_Ctx_Text_Opt opt);
#define mel_draw_ctx_text(ctx, ...) mel_draw_ctx_text_opt((ctx), (Mel_Draw_Ctx_Text_Opt){__VA_ARGS__})

void mel_draw_ctx_commit(Mel_Draw_Ctx* ctx);
void mel_draw_ctx_render(Mel_Draw_Ctx* ctx, VkCommandBuffer cmd, Mel_Mat4* projection);
