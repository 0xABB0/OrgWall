#ifndef MEL_SPRITE_BATCH_H
#define MEL_SPRITE_BATCH_H

#include "vk_context.h"
#include "vk_buffer.h"
#include "vk_pipeline.h"
#include "vk_texture.h"
#include "vec4.h"
#include "mat4.h"

typedef struct
{
    f32 x, y;
    f32 u, v;
    f32 r, g, b, a;
} Mel_SpriteVertex;

typedef struct
{
    Mel_VkBuffer vertex_buffer;
    Mel_VkBuffer index_buffer;

    Mel_SpriteVertex* vertices;
    u16* indices;
    u32 vertex_count;
    u32 index_count;
    u32 max_sprites;

    Mel_VkPipeline* pipeline;
    VkDescriptorSet current_descriptor;
    Mel_VkTexture* current_texture;
} Mel_SpriteBatch;

typedef struct
{
    u32 max_sprites;
} Mel_SpriteBatch_Opt;

bool mel_sprite_batch_init_opt(Mel_SpriteBatch* batch, Mel_VkContext* ctx, Mel_SpriteBatch_Opt opt);
#define mel_sprite_batch_init(batch, ctx, ...) mel_sprite_batch_init_opt((batch), (ctx), (Mel_SpriteBatch_Opt){__VA_ARGS__})

void mel_sprite_batch_shutdown(Mel_SpriteBatch* batch, Mel_VkContext* ctx);

void mel_sprite_batch_begin(Mel_SpriteBatch* batch, Mel_VkPipeline* pipeline);
void mel_sprite_batch_end(Mel_SpriteBatch* batch, Mel_VkContext* ctx, VkCommandBuffer cmd, Mel_Mat4* projection);

void mel_sprite_batch_set_texture(Mel_SpriteBatch* batch, Mel_VkContext* ctx, Mel_VkTexture* texture);

void mel_sprite_batch_draw(Mel_SpriteBatch* batch, f32 x, f32 y, f32 w, f32 h, Mel_Vec4 color);
void mel_sprite_batch_draw_uv(Mel_SpriteBatch* batch, f32 x, f32 y, f32 w, f32 h, f32 u0, f32 v0, f32 u1, f32 v1, Mel_Vec4 color);

#endif
