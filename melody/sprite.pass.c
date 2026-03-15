#include "sprite.pass.h"
#include "render.list.h"
#include "render.pass.h"
#include "render.camera.h"
#include "render.material.h"
#include "texture.pool.h"
#include "gpu.pipeline.h"
#include "gpu.texture.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "string.str8.h"

#include <tracy/TracyC.h>
#include <string.h>

typedef struct
{
    f32 x, y;
    f32 u, v;
    f32 r, g, b, a;
} Mel_SpriteVertex;

static const char* SPRITE_SHADER_SOURCE =
"struct VSInput\n"
"{\n"
"    float2 position : POSITION;\n"
"    float2 texcoord : TEXCOORD0;\n"
"    float4 color : COLOR0;\n"
"};\n"
"\n"
"struct VSOutput\n"
"{\n"
"    float4 position : SV_Position;\n"
"    float2 texcoord : TEXCOORD0;\n"
"    float4 color : COLOR0;\n"
"};\n"
"\n"
"struct PushConstants\n"
"{\n"
"    float4x4 projection;\n"
"};\n"
"\n"
"[[vk::push_constant]]\n"
"PushConstants push;\n"
"\n"
"[[vk::binding(0, 0)]] Sampler2D tex;\n"
"\n"
"[shader(\"vertex\")]\n"
"VSOutput vertexMain(VSInput input)\n"
"{\n"
"    VSOutput output;\n"
"    output.position = mul(push.projection, float4(input.position, 0.0, 1.0));\n"
"    output.texcoord = input.texcoord;\n"
"    output.color = input.color;\n"
"    return output;\n"
"}\n"
"\n"
"[shader(\"fragment\")]\n"
"float4 fragmentMain(VSOutput input) : SV_Target\n"
"{\n"
"    return tex.Sample(input.texcoord) * input.color;\n"
"}\n";

static void mel__sprite_pass_push_draw(Mel_Sprite_Pass* sp)
{
    if (sp->draw_count > 0)
    {
        Mel_Sprite_Draw_Cmd* prev = &sp->draws[sp->draw_count - 1];
        prev->index_count = sp->index_count - prev->index_offset;
        if (prev->index_count == 0)
        {
            prev->descriptor = sp->current_descriptor;
            return;
        }
    }

    if (sp->draw_count >= sp->draw_capacity)
    {
        u32 new_cap = sp->draw_capacity == 0 ? 8 : sp->draw_capacity * 2;
        usize new_size = sizeof(Mel_Sprite_Draw_Cmd) * new_cap;
        if (sp->draws == NULL)
            sp->draws = mel_alloc(sp->alloc, new_size);
        else
            sp->draws = mel_realloc(sp->alloc, sp->draws, new_size);
        sp->draw_capacity = new_cap;
    }

    sp->draws[sp->draw_count++] = (Mel_Sprite_Draw_Cmd){
        .descriptor = sp->current_descriptor,
        .index_offset = sp->index_count,
        .index_count = 0,
    };
}

static void mel__sprite_pass_set_texture(Mel_Sprite_Pass* sp, Mel_Gpu_Texture* texture)
{
    assert(sp != nullptr);

    if (sp->current_texture == texture) return;

    sp->current_texture = texture;
    sp->current_descriptor = texture ? texture->descriptor : VK_NULL_HANDLE;

    mel__sprite_pass_push_draw(sp);
}

static void mel__sprite_pass_push_quad(Mel_Sprite_Pass* sp, f32 x, f32 y, f32 w, f32 h, f32 u0, f32 v0, f32 u1, f32 v1, Mel_Vec4 color)
{
    assert(sp != nullptr);

    if (sp->vertex_count / 4 >= sp->max_sprites)
    {
        SDL_Log("Sprite pass overflow!");
        return;
    }

    Mel_SpriteVertex* vertices = (Mel_SpriteVertex*)sp->vertices;
    u32 vi = sp->vertex_count;
    u32 ii = sp->index_count;

    vertices[vi + 0] = (Mel_SpriteVertex){ x,     y,     u0, v0, color.x, color.y, color.z, color.w };
    vertices[vi + 1] = (Mel_SpriteVertex){ x + w, y,     u1, v0, color.x, color.y, color.z, color.w };
    vertices[vi + 2] = (Mel_SpriteVertex){ x + w, y + h, u1, v1, color.x, color.y, color.z, color.w };
    vertices[vi + 3] = (Mel_SpriteVertex){ x,     y + h, u0, v1, color.x, color.y, color.z, color.w };

    sp->indices[ii + 0] = (u16)(vi + 0);
    sp->indices[ii + 1] = (u16)(vi + 1);
    sp->indices[ii + 2] = (u16)(vi + 2);
    sp->indices[ii + 3] = (u16)(vi + 2);
    sp->indices[ii + 4] = (u16)(vi + 3);
    sp->indices[ii + 5] = (u16)(vi + 0);

    sp->vertex_count += 4;
    sp->index_count += 6;
}

static void mel__sprite_pass_push_line(Mel_Sprite_Pass* sp, f32 x0, f32 y0, f32 x1, f32 y1, f32 thickness, Mel_Vec4 color)
{
    assert(sp != nullptr);

    f32 dx = x1 - x0;
    f32 dy = y1 - y0;
    f32 len = sqrtf(dx * dx + dy * dy);
    if (len < 1e-6f) return;

    if (sp->vertex_count / 4 >= sp->max_sprites)
    {
        SDL_Log("Sprite pass overflow!");
        return;
    }

    f32 inv_len = 1.0f / len;
    f32 px = -dy * inv_len * thickness * 0.5f;
    f32 py =  dx * inv_len * thickness * 0.5f;

    Mel_SpriteVertex* vertices = (Mel_SpriteVertex*)sp->vertices;
    u32 vi = sp->vertex_count;
    u32 ii = sp->index_count;

    vertices[vi + 0] = (Mel_SpriteVertex){ x0 - px, y0 - py, 0, 0, color.x, color.y, color.z, color.w };
    vertices[vi + 1] = (Mel_SpriteVertex){ x0 + px, y0 + py, 1, 0, color.x, color.y, color.z, color.w };
    vertices[vi + 2] = (Mel_SpriteVertex){ x1 + px, y1 + py, 1, 1, color.x, color.y, color.z, color.w };
    vertices[vi + 3] = (Mel_SpriteVertex){ x1 - px, y1 - py, 0, 1, color.x, color.y, color.z, color.w };

    sp->indices[ii + 0] = (u16)(vi + 0);
    sp->indices[ii + 1] = (u16)(vi + 1);
    sp->indices[ii + 2] = (u16)(vi + 2);
    sp->indices[ii + 3] = (u16)(vi + 2);
    sp->indices[ii + 4] = (u16)(vi + 3);
    sp->indices[ii + 5] = (u16)(vi + 0);

    sp->vertex_count += 4;
    sp->index_count += 6;
}

static void mel__sprite_pass_begin(Mel_Sprite_Pass* sp, u32 frame_index)
{
    TracyCZoneN(ctx, "sprite_pass_begin", true);
    assert(sp != nullptr);
    assert(frame_index < MEL_MAX_FRAMES_IN_FLIGHT);

    sp->gpu_frame_index = frame_index;
    sp->vertices = sp->gpu_frames[frame_index].vertex_buffer.mapped;
    sp->indices = (u16*)sp->gpu_frames[frame_index].index_buffer.mapped;

    sp->vertex_count = 0;
    sp->index_count = 0;
    sp->draw_count = 0;
    sp->current_texture = nullptr;
    sp->current_descriptor = VK_NULL_HANDLE;

    mel__sprite_pass_set_texture(sp, &sp->white_texture);
    TracyCZoneEnd(ctx);
}

static void mel__sprite_pass_flush(Mel_Sprite_Pass* sp, Mel_Gpu_Cmd cmd, Mel_Mat4* projection)
{
    TracyCZoneN(ctx, "sprite_pass_flush", true);
    assert(sp != nullptr);

    if (sp->draw_count > 0)
    {
        Mel_Sprite_Draw_Cmd* last = &sp->draws[sp->draw_count - 1];
        last->index_count = sp->index_count - last->index_offset;
    }

    if (sp->index_count == 0)
    {
        TracyCZoneEnd(ctx);
        return;
    }

    Mel_Sprite_Gpu_Frame* frame = &sp->gpu_frames[sp->gpu_frame_index];

    TracyCZoneN(ctx_flush, "buffer_flush", true);
    mel_gpu_buffer_flush(&frame->vertex_buffer, cmd.dev);
    mel_gpu_buffer_flush(&frame->index_buffer, cmd.dev);
    TracyCZoneEnd(ctx_flush);

    mel_gpu_pipeline_bind(&sp->pipeline, cmd.cmd);

    vkCmdPushConstants(cmd.cmd, sp->pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mel_Mat4), projection);

    VkBuffer buffers[] = { frame->vertex_buffer.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd.cmd, 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(cmd.cmd, frame->index_buffer.buffer, 0, VK_INDEX_TYPE_UINT16);

    TracyCZoneN(ctx_draw, "vkCmdDrawIndexed", true);
    for (u32 i = 0; i < sp->draw_count; i++)
    {
        Mel_Sprite_Draw_Cmd* draw = &sp->draws[i];
        if (draw->index_count == 0) continue;

        if (draw->descriptor)
        {
            vkCmdBindDescriptorSets(cmd.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sp->pipeline.layout,
                0, 1, &draw->descriptor, 0, nullptr);
        }

        vkCmdDrawIndexed(cmd.cmd, draw->index_count, 1, draw->index_offset, 0, 0);
    }
    TracyCZoneEnd(ctx_draw);

    TracyCZoneEnd(ctx);
}

bool mel_sprite_pass_init_opt(Mel_Sprite_Pass* sp, Mel_Sprite_Pass_Init_Opt opt)
{
    assert(sp != nullptr);
    assert(opt.dev != nullptr);

    *sp = (Mel_Sprite_Pass){0};
    sp->dev = opt.dev;
    sp->alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    mel_gpu_shader_init(&sp->shader, opt.dev,
        .source = str8_from_cstr(SPRITE_SHADER_SOURCE));

    VkVertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(Mel_SpriteVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    VkVertexInputAttributeDescription attributes[] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,
          .offset = offsetof(Mel_SpriteVertex, x) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT,
          .offset = offsetof(Mel_SpriteVertex, u) },
        { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT,
          .offset = offsetof(Mel_SpriteVertex, r) },
    };

    mel_gpu_pipeline_init(&sp->pipeline, opt.dev,
        .shader = &sp->shader,
        .color_format = opt.color_format,
        .bindings = &binding,
        .binding_count = 1,
        .attributes = attributes,
        .attribute_count = 3,
        .push_constant_size = sizeof(Mel_Mat4),
        .use_texture = true,
        .blend_mode = MEL_GPU_BLEND_ALPHA);

    u32 max_sprites = opt.max_sprites > 0 ? opt.max_sprites : 4096;
    sp->max_sprites = max_sprites;

    u32 vertex_size = max_sprites * 4 * sizeof(Mel_SpriteVertex);
    u32 index_size = max_sprites * 6 * sizeof(u16);

    for (u32 i = 0; i < MEL_MAX_FRAMES_IN_FLIGHT; i++)
    {
        mel_gpu_buffer_init(&sp->gpu_frames[i].vertex_buffer, opt.dev,
            .size = vertex_size,
            .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            .memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU);

        mel_gpu_buffer_init(&sp->gpu_frames[i].index_buffer, opt.dev,
            .size = index_size,
            .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            .memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU);
    }

    sp->gpu_frame_index = 0;
    sp->vertices = sp->gpu_frames[0].vertex_buffer.mapped;
    sp->indices = (u16*)sp->gpu_frames[0].index_buffer.mapped;

    mel_gpu_texture_init_white(&sp->white_texture, opt.dev);
    sp->white_texture.descriptor = mel_gpu_pipeline_alloc_descriptor(&sp->pipeline, opt.dev);
    mel_gpu_pipeline_write_texture(&sp->pipeline, opt.dev, sp->white_texture.descriptor,
        sp->white_texture.image.view, sp->white_texture.sampler);

    return true;
}

void mel_sprite_pass_shutdown(Mel_Sprite_Pass* sp)
{
    assert(sp != nullptr);
    assert(sp->dev != nullptr);

    if (sp->draws)
        mel_dealloc(sp->alloc, sp->draws);

    for (u32 i = 0; i < MEL_MAX_FRAMES_IN_FLIGHT; i++)
    {
        mel_gpu_buffer_shutdown(&sp->gpu_frames[i].index_buffer, sp->dev);
        mel_gpu_buffer_shutdown(&sp->gpu_frames[i].vertex_buffer, sp->dev);
    }
    mel_gpu_texture_shutdown(&sp->white_texture, sp->dev);
    mel_gpu_pipeline_shutdown(&sp->pipeline, sp->dev);
    mel_gpu_shader_shutdown(&sp->shader, sp->dev);
    sp->dev = nullptr;
}

static void mel__sprite_pass_draw_entry(Mel_Sprite_Pass* sp, Mel_Sprite_Entry* entry)
{
    if (sp->pool)
    {
        Mel_Gpu_Texture* tex = mel_texture_pool_get(sp->pool, entry->tex);
        mel__sprite_pass_set_texture(sp, tex);
    }

    Mel_Rect uv = entry->uv;
    if (uv.w == 0.0f && uv.h == 0.0f)
        uv = (Mel_Rect){0, 0, 1, 1};

    Mel_Vec4 color = entry->color;
    if (mel_material_instance_handle_valid(entry->material))
        color = mel_vec4_mul(color, mel_material_instance_base_color(entry->material));

    mel__sprite_pass_push_quad(sp,
        entry->pos.x, entry->pos.y, entry->size.x, entry->size.y,
        uv.x, uv.y, uv.x + uv.w, uv.y + uv.h,
        color);
}

void mel_sprite_pass_draw(Mel_Render_List* list, Mel_Sprite_Pass* sp)
{
    assert(list != nullptr);
    assert(sp != nullptr);

    if (list->dirty)
        mel_render_list_sort(list);

    for (u32 i = 0; i < list->count; i++)
    {
        Mel_Sprite_Entry* entry = mel_render_list_get(list, list->packets[i].entry_index);
        mel__sprite_pass_draw_entry(sp, entry);
    }
}

void mel_sprite_pass_execute(Mel_Render_Pass_Ctx* ctx)
{
    Mel_Sprite_Pass* sp = ctx->user;
    assert(sp != nullptr);

    Mel_Mat4 vp = mel_camera_vp(ctx->camera);

    mel__sprite_pass_begin(sp, ctx->gpu_frame_index);

    if (ctx->read_lists)
    {
        u32 list_count = 0;
        for (u32 i = 0; ctx->read_lists[i] != nullptr; i++)
        {
            if (ctx->read_lists[i]->dirty)
                mel_render_list_sort(ctx->read_lists[i]);
            list_count++;
        }

        if (list_count == 1)
        {
            mel_sprite_pass_draw(ctx->read_lists[0], sp);
        }
        else if (list_count > 1)
        {
            u32 cursors[list_count];
            memset(cursors, 0, sizeof(u32) * list_count);

            u32 total = 0;
            for (u32 i = 0; i < list_count; i++)
                total += ctx->read_lists[i]->count;

            for (u32 drawn = 0; drawn < total; drawn++)
            {
                u32 best = UINT32_MAX;
                u64 best_key = UINT64_MAX;
                for (u32 i = 0; i < list_count; i++)
                {
                    Mel_Render_List* list = ctx->read_lists[i];
                    if (cursors[i] >= list->count)
                        continue;
                    u64 key = list->packets[cursors[i]].sort_key;
                    if (key < best_key)
                    {
                        best_key = key;
                        best = i;
                    }
                }
                if (best == UINT32_MAX) break;

                Mel_Render_List* list = ctx->read_lists[best];
                Mel_Sprite_Entry* entry = mel_render_list_get(list, list->packets[cursors[best]].entry_index);
                cursors[best]++;
                mel__sprite_pass_draw_entry(sp, entry);
            }
        }
    }

    mel__sprite_pass_flush(sp, ctx->cmd, &vp);
}

void mel_draw_sprite_opt(Mel_Render_List* list, Mel_Draw_Sprite_Opt opt)
{
    assert(list != nullptr);

    Mel_Rect uv = opt.uv;
    if (uv.w == 0 && uv.h == 0) uv = MEL_UV_FULL;

    u64 key = mel_sort_key_sprite(opt.layer, opt.depth, 0, mel_texture_bucket(opt.tex));
    Mel_Sprite_Entry* e = mel_render_list_push(list, key);
    *e = (Mel_Sprite_Entry){
        .pos = opt.pos,
        .size = opt.size,
        .uv = uv,
        .color = opt.color,
        .tex = opt.tex,
        .material = opt.material,
    };
}
