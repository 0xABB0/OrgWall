#include "render.draw.h"
#include "gpu.device.h"
#include "gpu.pipeline.h"
#include "gpu.cmd.h"
#include "gpu.texture.h"
#include "gpu.types.vulkan.h"
#include "font.atlas.h"
#include "texture.pool.h"
#include "string.str8.h"
#include "allocator.h"
#include "allocator.heap.h"
#include <string.h>

static void mel__draw_ctx_ensure_capacity(Mel_Draw_Ctx* ctx, u32 extra_verts, u32 extra_indices)
{
    if (ctx->vertex_count + extra_verts > ctx->vertex_capacity)
    {
        u32 new_cap = ctx->vertex_capacity == 0 ? 256 : ctx->vertex_capacity * 2;
        while (new_cap < ctx->vertex_count + extra_verts) new_cap *= 2;
        usize new_size = sizeof(Mel_Draw_Vertex) * new_cap;
        if (ctx->vertices == nullptr)
            ctx->vertices = mel_alloc(ctx->alloc, new_size);
        else
            ctx->vertices = mel_realloc(ctx->alloc, ctx->vertices, new_size);
        ctx->vertex_capacity = new_cap;
    }

    if (ctx->index_count + extra_indices > ctx->index_capacity)
    {
        u32 new_cap = ctx->index_capacity == 0 ? 384 : ctx->index_capacity * 2;
        while (new_cap < ctx->index_count + extra_indices) new_cap *= 2;
        usize new_size = sizeof(u16) * new_cap;
        if (ctx->indices == nullptr)
            ctx->indices = mel_alloc(ctx->alloc, new_size);
        else
            ctx->indices = mel_realloc(ctx->alloc, ctx->indices, new_size);
        ctx->index_capacity = new_cap;
    }
}

static void mel__draw_ctx_push_draw(Mel_Draw_Ctx* ctx)
{
    if (ctx->draw_count > 0)
    {
        Mel_Draw_Cmd* prev = &ctx->draws[ctx->draw_count - 1];
        prev->index_count = ctx->index_count - prev->index_offset;
        if (prev->index_count == 0) return;
    }

    if (ctx->draw_count >= ctx->draw_capacity)
    {
        u32 new_cap = ctx->draw_capacity == 0 ? 8 : ctx->draw_capacity * 2;
        usize new_size = sizeof(Mel_Draw_Cmd) * new_cap;
        if (ctx->draws == nullptr)
            ctx->draws = mel_alloc(ctx->alloc, new_size);
        else
            ctx->draws = mel_realloc(ctx->alloc, ctx->draws, new_size);
        ctx->draw_capacity = new_cap;
    }

    ctx->draws[ctx->draw_count++] = (Mel_Draw_Cmd){
        ._descriptor = ctx->_current_descriptor,
        .index_offset = ctx->index_count,
        .index_count = 0,
    };
}

static void mel__draw_ctx_set_texture(Mel_Draw_Ctx* ctx, Mel_Gpu_Texture* texture)
{
    assert(ctx != nullptr);

    if (ctx->current_texture == texture) return;

    ctx->current_texture = texture;
    ctx->_current_descriptor = texture ? texture->_descriptor : nullptr;

    mel__draw_ctx_push_draw(ctx);
}

void mel_draw_ctx_init_opt(Mel_Draw_Ctx* ctx, Mel_Draw_Ctx_Opt opt)
{
    assert(ctx != nullptr);
    *ctx = (Mel_Draw_Ctx){0};
    ctx->pipeline = opt.pipeline;
    ctx->default_texture = opt.texture;
    ctx->current_texture = opt.texture;
    ctx->_current_descriptor = opt.texture ? opt.texture->_descriptor : nullptr;
    ctx->pool = opt.pool;
    ctx->dev = opt.dev;
    ctx->alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    if (ctx->default_texture)
        mel__draw_ctx_push_draw(ctx);
}

void mel_draw_ctx_shutdown(Mel_Draw_Ctx* ctx)
{
    assert(ctx != nullptr);

    for (u32 i = 0; i < MEL_MAX_FRAMES_IN_FLIGHT; i++)
    {
        Mel_Draw_Gpu_Frame* f = &ctx->gpu_frames[i];
        if (f->vertex_buffer._handle) mel_gpu_buffer_shutdown(&f->vertex_buffer, ctx->dev);
        if (f->index_buffer._handle) mel_gpu_buffer_shutdown(&f->index_buffer, ctx->dev);
    }

    if (ctx->vertices) mel_dealloc(ctx->alloc, ctx->vertices);
    if (ctx->indices) mel_dealloc(ctx->alloc, ctx->indices);
    if (ctx->draws) mel_dealloc(ctx->alloc, ctx->draws);
}

void mel_draw_ctx_clear(Mel_Draw_Ctx* ctx)
{
    assert(ctx != nullptr);
    ctx->vertex_count = 0;
    ctx->index_count = 0;
    ctx->draw_count = 0;
    ctx->committed = false;

    ctx->gpu_frame_index = (ctx->gpu_frame_index + 1) % MEL_MAX_FRAMES_IN_FLIGHT;

    ctx->current_texture = ctx->default_texture;
    ctx->_current_descriptor = ctx->default_texture ? ctx->default_texture->_descriptor : nullptr;

    if (ctx->default_texture)
        mel__draw_ctx_push_draw(ctx);
}

void mel_draw_ctx_rect(Mel_Draw_Ctx* ctx, f32 x, f32 y, f32 w, f32 h, Mel_Vec4 color)
{
    assert(ctx != nullptr);
    mel__draw_ctx_set_texture(ctx, ctx->default_texture);
    mel__draw_ctx_ensure_capacity(ctx, 4, 6);

    u32 vi = ctx->vertex_count;
    u32 ii = ctx->index_count;

    ctx->vertices[vi + 0] = (Mel_Draw_Vertex){ x,     y,     0, 0, color.r, color.g, color.b, color.a };
    ctx->vertices[vi + 1] = (Mel_Draw_Vertex){ x + w, y,     1, 0, color.r, color.g, color.b, color.a };
    ctx->vertices[vi + 2] = (Mel_Draw_Vertex){ x + w, y + h, 1, 1, color.r, color.g, color.b, color.a };
    ctx->vertices[vi + 3] = (Mel_Draw_Vertex){ x,     y + h, 0, 1, color.r, color.g, color.b, color.a };

    ctx->indices[ii + 0] = (u16)(vi + 0);
    ctx->indices[ii + 1] = (u16)(vi + 1);
    ctx->indices[ii + 2] = (u16)(vi + 2);
    ctx->indices[ii + 3] = (u16)(vi + 2);
    ctx->indices[ii + 4] = (u16)(vi + 3);
    ctx->indices[ii + 5] = (u16)(vi + 0);

    ctx->vertex_count += 4;
    ctx->index_count += 6;
}

void mel_draw_ctx_line(Mel_Draw_Ctx* ctx, Mel_Vec2 from, Mel_Vec2 to, f32 thickness, Mel_Vec4 color)
{
    assert(ctx != nullptr);
    mel__draw_ctx_set_texture(ctx, ctx->default_texture);

    Mel_Vec2 dir = mel_vec2_sub(to, from);
    f32 len = mel_vec2_len(dir);
    if (len < 1e-6f) return;

    dir = mel_vec2_scale(dir, 1.0f / len);
    Mel_Vec2 perp = mel_vec2(-dir.y, dir.x);
    perp = mel_vec2_scale(perp, thickness * 0.5f);

    mel__draw_ctx_ensure_capacity(ctx, 4, 6);

    u32 vi = ctx->vertex_count;
    u32 ii = ctx->index_count;

    ctx->vertices[vi + 0] = (Mel_Draw_Vertex){ from.x - perp.x, from.y - perp.y, 0, 0, color.r, color.g, color.b, color.a };
    ctx->vertices[vi + 1] = (Mel_Draw_Vertex){ from.x + perp.x, from.y + perp.y, 1, 0, color.r, color.g, color.b, color.a };
    ctx->vertices[vi + 2] = (Mel_Draw_Vertex){ to.x + perp.x,   to.y + perp.y,   1, 1, color.r, color.g, color.b, color.a };
    ctx->vertices[vi + 3] = (Mel_Draw_Vertex){ to.x - perp.x,   to.y - perp.y,   0, 1, color.r, color.g, color.b, color.a };

    ctx->indices[ii + 0] = (u16)(vi + 0);
    ctx->indices[ii + 1] = (u16)(vi + 1);
    ctx->indices[ii + 2] = (u16)(vi + 2);
    ctx->indices[ii + 3] = (u16)(vi + 2);
    ctx->indices[ii + 4] = (u16)(vi + 3);
    ctx->indices[ii + 5] = (u16)(vi + 0);

    ctx->vertex_count += 4;
    ctx->index_count += 6;
}

void mel_draw_ctx_text_opt(Mel_Draw_Ctx* ctx, Mel_Draw_Ctx_Text_Opt opt)
{
    assert(ctx != nullptr);
    assert(ctx->pool != nullptr);

    if (str8_is_empty(opt.text)) return;

    Mel_Font_Atlas_Entry* font = mel_font_atlas_get(opt.font);
    assert(font != nullptr);

    Mel_Gpu_Texture* tex = mel_texture_pool_get(ctx->pool, font->tex_handle);
    mel__draw_ctx_set_texture(ctx, tex);

    f32 cursor_x = opt.x;
    f32 cursor_y = opt.y + font->desc.ascent;

    for (size i = 0; i < opt.text.len; i++)
    {
        int c = opt.text.data[i];

        if (c == '\n')
        {
            cursor_x = opt.x;
            cursor_y += font->desc.line_height;
            continue;
        }

        if (c < (int)font->desc.first_codepoint ||
            c >= (int)(font->desc.first_codepoint + font->desc.glyph_count))
            continue;

        int idx = c - (int)font->desc.first_codepoint;
        Mel_Font_Glyph* g = &font->desc.glyphs[idx];

        f32 gx = cursor_x + g->x0;
        f32 gy = cursor_y + g->y0;
        f32 gw = g->x1 - g->x0;
        f32 gh = g->y1 - g->y0;

        if (gw > 0 && gh > 0)
        {
            mel__draw_ctx_ensure_capacity(ctx, 4, 6);

            u32 vi = ctx->vertex_count;
            u32 ii = ctx->index_count;

            ctx->vertices[vi + 0] = (Mel_Draw_Vertex){ gx,      gy,      g->u0, g->v0, opt.color.r, opt.color.g, opt.color.b, opt.color.a };
            ctx->vertices[vi + 1] = (Mel_Draw_Vertex){ gx + gw, gy,      g->u1, g->v0, opt.color.r, opt.color.g, opt.color.b, opt.color.a };
            ctx->vertices[vi + 2] = (Mel_Draw_Vertex){ gx + gw, gy + gh, g->u1, g->v1, opt.color.r, opt.color.g, opt.color.b, opt.color.a };
            ctx->vertices[vi + 3] = (Mel_Draw_Vertex){ gx,      gy + gh, g->u0, g->v1, opt.color.r, opt.color.g, opt.color.b, opt.color.a };

            ctx->indices[ii + 0] = (u16)(vi + 0);
            ctx->indices[ii + 1] = (u16)(vi + 1);
            ctx->indices[ii + 2] = (u16)(vi + 2);
            ctx->indices[ii + 3] = (u16)(vi + 2);
            ctx->indices[ii + 4] = (u16)(vi + 3);
            ctx->indices[ii + 5] = (u16)(vi + 0);

            ctx->vertex_count += 4;
            ctx->index_count += 6;
        }

        cursor_x += g->xadvance;
    }
}

void mel_draw_ctx_commit(Mel_Draw_Ctx* ctx)
{
    assert(ctx != nullptr);
    assert(ctx->dev != nullptr);

    if (ctx->vertex_count == 0)
    {
        ctx->committed = true;
        return;
    }

    if (ctx->draw_count > 0)
    {
        Mel_Draw_Cmd* last = &ctx->draws[ctx->draw_count - 1];
        last->index_count = ctx->index_count - last->index_offset;
    }

    Mel_Draw_Gpu_Frame* f = &ctx->gpu_frames[ctx->gpu_frame_index];

    if (ctx->vertex_count > f->vertex_capacity)
    {
        if (f->vertex_buffer._handle)
            mel_gpu_buffer_shutdown(&f->vertex_buffer, ctx->dev);
        mel_gpu_buffer_init(&f->vertex_buffer, ctx->dev,
            .size = ctx->vertex_count * sizeof(Mel_Draw_Vertex),
            .usage = MEL_GPU_BUFFER_USAGE_VERTEX,
            .memory_usage = MEL_GPU_MEMORY_USAGE_CPU_TO_GPU);
        f->vertex_capacity = ctx->vertex_count;
    }

    if (ctx->index_count > f->index_capacity)
    {
        if (f->index_buffer._handle)
            mel_gpu_buffer_shutdown(&f->index_buffer, ctx->dev);
        mel_gpu_buffer_init(&f->index_buffer, ctx->dev,
            .size = ctx->index_count * sizeof(u16),
            .usage = MEL_GPU_BUFFER_USAGE_INDEX,
            .memory_usage = MEL_GPU_MEMORY_USAGE_CPU_TO_GPU);
        f->index_capacity = ctx->index_count;
    }

    memcpy(f->vertex_buffer.mapped, ctx->vertices, ctx->vertex_count * sizeof(Mel_Draw_Vertex));
    memcpy(f->index_buffer.mapped, ctx->indices, ctx->index_count * sizeof(u16));

    mel_gpu_buffer_flush(&f->vertex_buffer, ctx->dev);
    mel_gpu_buffer_flush(&f->index_buffer, ctx->dev);

    ctx->committed = true;
}

void mel_draw_ctx_render(Mel_Draw_Ctx* ctx, Mel_Gpu_Cmd* cmd, Mel_Mat4* projection)
{
    assert(ctx != nullptr);
    assert(ctx->committed);

    if (ctx->index_count == 0) return;

    Mel_Draw_Gpu_Frame* f = &ctx->gpu_frames[ctx->gpu_frame_index];

    mel_gpu_pipeline_bind(ctx->pipeline, cmd);

    mel_gpu_cmd_push_constants(cmd, ctx->pipeline, MEL_GPU_SHADER_STAGE_VERTEX,
                               0, sizeof(Mel_Mat4), projection);

    mel_gpu_cmd_bind_vertex_buffer(cmd, &f->vertex_buffer, 0);
    mel_gpu_cmd_bind_index_buffer(cmd, &f->index_buffer, 0, MEL_GPU_INDEX_TYPE_U16);

    for (u32 i = 0; i < ctx->draw_count; i++)
    {
        Mel_Draw_Cmd* draw = &ctx->draws[i];
        if (draw->index_count == 0) continue;

        if (draw->_descriptor)
        {
            mel_gpu_cmd_bind_descriptor_set(cmd, ctx->pipeline, draw->_descriptor);
        }

        mel_gpu_cmd_draw_indexed(cmd, draw->index_count, 1, draw->index_offset, 0, 0);
    }
}
