#define VK_NO_PROTOTYPES
#include "sprite.batch.h"
#include "gpu.pipeline.h"
#include "gpu.texture.h"
#include "allocator.h"
#include "allocator.heap.h"
#include <string.h>
#include <tracy/TracyC.h>

static void mel__sprite_batch_push_draw(Mel_SpriteBatch* batch)
{
    if (batch->draw_count > 0)
    {
        Mel_SpriteBatch_Draw* prev = &batch->draws[batch->draw_count - 1];
        prev->index_count = batch->index_count - prev->index_offset;
        if (prev->index_count == 0) return;
    }

    if (batch->draw_count >= batch->draw_capacity)
    {
        u32 new_cap = batch->draw_capacity == 0 ? 8 : batch->draw_capacity * 2;
        usize new_size = sizeof(Mel_SpriteBatch_Draw) * new_cap;
        if (batch->draws == NULL)
            batch->draws = mel_alloc(batch->allocator, new_size);
        else
            batch->draws = mel_realloc(batch->allocator, batch->draws, new_size);
        batch->draw_capacity = new_cap;
    }

    batch->draws[batch->draw_count++] = (Mel_SpriteBatch_Draw){
        .descriptor = batch->current_descriptor,
        .index_offset = batch->index_count,
        .index_count = 0,
    };
}

bool mel_sprite_batch_init_opt(Mel_SpriteBatch* batch, Mel_Gpu_Device* dev, Mel_SpriteBatch_Opt opt)
{
    assert(batch != nullptr);
    assert(dev != nullptr);

    *batch = (Mel_SpriteBatch){0};

    batch->allocator = opt.allocator ? opt.allocator : mel_alloc_heap();

    u32 max_sprites = opt.max_sprites > 0 ? opt.max_sprites : 1024;
    batch->max_sprites = max_sprites;

    u32 vertex_size = max_sprites * 4 * sizeof(Mel_SpriteVertex);
    u32 index_size = max_sprites * 6 * sizeof(u16);

    mel_gpu_buffer_init(&batch->vertex_buffer, dev,
        .size = vertex_size,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU);

    mel_gpu_buffer_init(&batch->index_buffer, dev,
        .size = index_size,
        .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        .memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU);

    batch->vertices = (Mel_SpriteVertex*)batch->vertex_buffer.mapped;
    batch->indices = (u16*)batch->index_buffer.mapped;

    return true;
}

void mel_sprite_batch_shutdown(Mel_SpriteBatch* batch, Mel_Gpu_Device* dev)
{
    assert(batch != nullptr);
    assert(dev != nullptr);

    if (batch->draws)
        mel_dealloc(batch->allocator, batch->draws);

    mel_gpu_buffer_shutdown(&batch->index_buffer, dev);
    mel_gpu_buffer_shutdown(&batch->vertex_buffer, dev);
}

void mel_sprite_batch_begin(Mel_SpriteBatch* batch, Mel_Gpu_Pipeline* pipeline)
{
    TracyCZoneN(ctx, "sprite_batch_begin", true);
    assert(batch != nullptr);
    assert(pipeline != nullptr);

    batch->vertex_count = 0;
    batch->index_count = 0;
    batch->draw_count = 0;
    batch->pipeline = pipeline;
    batch->current_texture = nullptr;
    batch->current_descriptor = VK_NULL_HANDLE;
    TracyCZoneEnd(ctx);
}

void mel_sprite_batch_end(Mel_SpriteBatch* batch, Mel_Gpu_Device* dev, VkCommandBuffer cmd, Mel_Mat4* projection)
{
    TracyCZoneN(ctx, "sprite_batch_end", true);
    assert(batch != nullptr);
    assert(dev != nullptr);

    if (batch->draw_count > 0)
    {
        Mel_SpriteBatch_Draw* last = &batch->draws[batch->draw_count - 1];
        last->index_count = batch->index_count - last->index_offset;
    }

    if (batch->index_count == 0)
    {
        TracyCZoneEnd(ctx);
        return;
    }

    TracyCZoneN(ctx_flush, "buffer_flush", true);
    mel_gpu_buffer_flush(&batch->vertex_buffer, dev);
    mel_gpu_buffer_flush(&batch->index_buffer, dev);
    TracyCZoneEnd(ctx_flush);

    mel_gpu_pipeline_bind(batch->pipeline, cmd);

    vkCmdPushConstants(cmd, batch->pipeline->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mel_Mat4), projection);

    VkBuffer buffers[] = { batch->vertex_buffer.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(cmd, batch->index_buffer.buffer, 0, VK_INDEX_TYPE_UINT16);

    TracyCZoneN(ctx_draw, "vkCmdDrawIndexed", true);
    for (u32 i = 0; i < batch->draw_count; i++)
    {
        Mel_SpriteBatch_Draw* draw = &batch->draws[i];
        if (draw->index_count == 0) continue;

        if (draw->descriptor)
        {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline->layout,
                0, 1, &draw->descriptor, 0, nullptr);
        }

        vkCmdDrawIndexed(cmd, draw->index_count, 1, draw->index_offset, 0, 0);
    }
    TracyCZoneEnd(ctx_draw);

    TracyCZoneEnd(ctx);
}

void mel_sprite_batch_set_texture(Mel_SpriteBatch* batch, Mel_Gpu_Texture* texture)
{
    assert(batch != nullptr);

    if (batch->current_texture == texture) return;

    batch->current_texture = texture;
    batch->current_descriptor = texture ? texture->descriptor : VK_NULL_HANDLE;

    mel__sprite_batch_push_draw(batch);
}

void mel_sprite_batch_draw_uv(Mel_SpriteBatch* batch, f32 x, f32 y, f32 w, f32 h, f32 u0, f32 v0, f32 u1, f32 v1, Mel_Vec4 color)
{
    assert(batch != nullptr);

    if (batch->vertex_count / 4 >= batch->max_sprites)
    {
        SDL_Log("SpriteBatch overflow!");
        return;
    }

    u32 vi = batch->vertex_count;
    u32 ii = batch->index_count;

    batch->vertices[vi + 0] = (Mel_SpriteVertex){ x,     y,     u0, v0, color.x, color.y, color.z, color.w };
    batch->vertices[vi + 1] = (Mel_SpriteVertex){ x + w, y,     u1, v0, color.x, color.y, color.z, color.w };
    batch->vertices[vi + 2] = (Mel_SpriteVertex){ x + w, y + h, u1, v1, color.x, color.y, color.z, color.w };
    batch->vertices[vi + 3] = (Mel_SpriteVertex){ x,     y + h, u0, v1, color.x, color.y, color.z, color.w };

    batch->indices[ii + 0] = (u16)(vi + 0);
    batch->indices[ii + 1] = (u16)(vi + 1);
    batch->indices[ii + 2] = (u16)(vi + 2);
    batch->indices[ii + 3] = (u16)(vi + 2);
    batch->indices[ii + 4] = (u16)(vi + 3);
    batch->indices[ii + 5] = (u16)(vi + 0);

    batch->vertex_count += 4;
    batch->index_count += 6;
}

void mel_sprite_batch_draw(Mel_SpriteBatch* batch, f32 x, f32 y, f32 w, f32 h, Mel_Vec4 color)
{
    mel_sprite_batch_draw_uv(batch, x, y, w, h, 0, 0, 1, 1, color);
}
