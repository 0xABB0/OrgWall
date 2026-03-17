#include "text.pass.h"
#include "gpu.device.h"
#include "gpu.types.vulkan.h"
#include "render.list.h"
#include "render.pass.h"
#include "render.camera.h"
#include "event.channel.h"
#include "boot.registry.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "string.str8.h"
#include "async.job.h"
#include "async.signal.h"

#include <SDL3/SDL.h>
#include <string.h>

static Mel_Text_Pass s_text_pass;

Mel_Text_Pass* mel_text_pass(void)
{
    return &s_text_pass;
}

Mel_Event_Channel mel_text_pass_ready;

static void mel__text_pass_compile_job(void* data)
{
    Mel_Gpu_Device* dev = (Mel_Gpu_Device*)data;

    mel_job_move_to_worker(0);

    mel_text_pass_init(&s_text_pass,
        .dev = dev,
        .color_format = MEL_GPU_FORMAT_B8G8R8A8_SRGB,
        .max_glyphs = 4096);

    mel_event_channel_fire(&mel_text_pass_ready, NULL);
}

static void mel__text_pass_on_gpu_ready(void* ctx, const void* event)
{
    (void)ctx;
    const Mel_Gpu_Ready_Event* e = event;
    mel_job_run(e->dev, mel__text_pass_compile_job, e->phase_counter);
}

static void mel__text_pass_wire(void)
{
    mel_event_channel_on(&mel_gpu_device_ready, mel__text_pass_on_gpu_ready, NULL);
}

__attribute__((constructor))
static void mel__text_pass_register(void)
{
    mel_event_channel_init(&mel_text_pass_ready, mel_alloc_heap());
    mel__boot_register_wire(mel__text_pass_wire);
}

__attribute__((destructor))
static void mel__text_pass_unregister(void)
{
    mel_event_channel_destroy(&mel_text_pass_ready);
}

typedef struct {
    f32 x, y;
    f32 u, v;
    f32 r, g, b, a;
    f32 or_, og, ob, oa;
    f32 mode, edge, softness, outline;
    f32 px_range, _pad0, _pad1, _pad2;
} Mel_Text_Vertex;

static const char* TEXT_SHADER_SOURCE =
"struct VSInput\n"
"{\n"
"    float2 position : POSITION;\n"
"    float2 texcoord : TEXCOORD0;\n"
"    float4 color : COLOR0;\n"
"    float4 outlineColor : COLOR1;\n"
"    float4 params0 : TEXCOORD1;\n"
"    float4 params1 : TEXCOORD2;\n"
"};\n"
"\n"
"struct VSOutput\n"
"{\n"
"    float4 position : SV_Position;\n"
"    float2 texcoord : TEXCOORD0;\n"
"    float4 color : COLOR0;\n"
"    float4 outlineColor : COLOR1;\n"
"    float4 params0 : TEXCOORD1;\n"
"    float4 params1 : TEXCOORD2;\n"
"};\n"
"\n"
"struct PushConstants\n"
"{\n"
"    float4x4 projection;\n"
"};\n"
"\n"
"[[vk::push_constant]] PushConstants push;\n"
"[[vk::binding(0, 0)]] Sampler2D tex;\n"
"\n"
"float median3(float r, float g, float b)\n"
"{\n"
"    return max(min(r, g), min(max(r, g), b));\n"
"}\n"
"\n"
"float computeScreenPxRange(float2 uv, float pxRange)\n"
"{\n"
"    uint texW = 1;\n"
"    uint texH = 1;\n"
"    tex.GetDimensions(texW, texH);\n"
"    float2 unitRange = float2(pxRange / max((float)texW, 1.0), pxRange / max((float)texH, 1.0));\n"
"    float2 screenTexSize = 1.0 / max(fwidth(uv), float2(1e-6, 1e-6));\n"
"    return max(0.5 * dot(unitRange, screenTexSize), 1.0);\n"
"}\n"
"\n"
"[shader(\"vertex\")]\n"
"VSOutput vertexMain(VSInput input)\n"
"{\n"
"    VSOutput output;\n"
"    output.position = mul(push.projection, float4(input.position, 0.0, 1.0));\n"
"    output.texcoord = input.texcoord;\n"
"    output.color = input.color;\n"
"    output.outlineColor = input.outlineColor;\n"
"    output.params0 = input.params0;\n"
"    output.params1 = input.params1;\n"
"    return output;\n"
"}\n"
"\n"
"[shader(\"fragment\")]\n"
"float4 fragmentMain(VSOutput input) : SV_Target\n"
"{\n"
"    float4 s = tex.Sample(input.texcoord);\n"
"    float mode = input.params0.x;\n"
"    float edge = input.params0.y;\n"
"    float softness = max(input.params0.z, 0.001);\n"
"    float outline = max(input.params0.w, 0.0);\n"
"    float pxRange = max(input.params1.x, 1.0);\n"
"    float dist = s.a;\n"
"    if (mode > 1.5)\n"
"        dist = median3(s.r, s.g, s.b);\n"
"    else if (mode > 0.5)\n"
"        dist = s.r;\n"
"    float fill = 0.0;\n"
"    float outlineFill = 0.0;\n"
"    if (mode > 0.5)\n"
"    {\n"
"        float screenPxRange = max(computeScreenPxRange(input.texcoord, pxRange), softness * 64.0);\n"
"        float signedDistance = screenPxRange * (dist - edge);\n"
"        fill = saturate(signedDistance + 0.5);\n"
"        if (outline > 0.0)\n"
"        {\n"
"            float outlineDistance = screenPxRange * (dist - (edge - outline));\n"
"            outlineFill = saturate(outlineDistance + 0.5);\n"
"        }\n"
"        else\n"
"        {\n"
"            outlineFill = fill;\n"
"        }\n"
"    }\n"
"    else\n"
"    {\n"
"        float aa = max(softness, fwidth(dist));\n"
"        fill = smoothstep(edge - aa, edge + aa, dist);\n"
"        outlineFill = outline > 0.0 ? smoothstep((edge - outline) - aa, (edge - outline) + aa, dist) : fill;\n"
"    }\n"
"    float outlineAlpha = saturate(outlineFill - fill);\n"
"    float fillAlpha = fill * input.color.a;\n"
"    float strokeAlpha = outlineAlpha * input.outlineColor.a;\n"
"    float alpha = saturate(fillAlpha + strokeAlpha);\n"
"    if (alpha <= 0.001)\n"
"        return float4(0.0, 0.0, 0.0, 0.0);\n"
"    float3 rgb = (input.color.rgb * fillAlpha + input.outlineColor.rgb * strokeAlpha) / alpha;\n"
"    return float4(rgb, alpha);\n"
"}\n";

static void* mel__text_pass_descriptor_for(Mel_Text_Pass* pass, Mel_Gpu_Texture* texture)
{
    for (u32 i = 0; i < pass->descriptor_cache_count; i++)
        if (pass->descriptor_cache[i].texture == texture)
            return pass->descriptor_cache[i]._descriptor;

    if (pass->descriptor_cache_count >= pass->descriptor_cache_capacity)
    {
        u32 new_cap = pass->descriptor_cache_capacity == 0 ? 8 : pass->descriptor_cache_capacity * 2;
        usize size = sizeof(Mel_Text_Descriptor_Cache_Entry) * new_cap;
        pass->descriptor_cache = pass->descriptor_cache
            ? mel_realloc(pass->alloc, pass->descriptor_cache, size)
            : mel_alloc(pass->alloc, size);
        pass->descriptor_cache_capacity = new_cap;
    }

    void* descriptor = mel_gpu_pipeline_alloc_descriptor(&pass->pipeline, pass->dev);
    mel_gpu_pipeline_write_texture(&pass->pipeline, pass->dev, descriptor,
        texture->image._view, texture->_sampler);

    pass->descriptor_cache[pass->descriptor_cache_count++] = (Mel_Text_Descriptor_Cache_Entry){
        .texture = texture,
        ._descriptor = descriptor,
    };
    return descriptor;
}

static void mel__text_pass_push_draw(Mel_Text_Pass* pass)
{
    if (pass->draw_count > 0)
    {
        Mel_Text_Draw_Cmd* prev = &pass->draws[pass->draw_count - 1];
        prev->index_count = pass->index_count - prev->index_offset;
        if (prev->index_count == 0)
        {
            prev->_descriptor = pass->_current_descriptor;
            return;
        }
    }

    if (pass->draw_count >= pass->draw_capacity)
    {
        u32 new_cap = pass->draw_capacity == 0 ? 8 : pass->draw_capacity * 2;
        usize new_size = sizeof(Mel_Text_Draw_Cmd) * new_cap;
        pass->draws = pass->draws ? mel_realloc(pass->alloc, pass->draws, new_size) : mel_alloc(pass->alloc, new_size);
        pass->draw_capacity = new_cap;
    }

    pass->draws[pass->draw_count++] = (Mel_Text_Draw_Cmd){
        ._descriptor = pass->_current_descriptor,
        .index_offset = pass->index_count,
        .index_count = 0,
    };
}

static void mel__text_pass_set_texture(Mel_Text_Pass* pass, Mel_Gpu_Texture* texture)
{
    if (!texture)
        texture = &pass->white_texture;
    if (pass->current_texture == texture)
        return;

    pass->current_texture = texture;
    pass->_current_descriptor = mel__text_pass_descriptor_for(pass, texture);
    mel__text_pass_push_draw(pass);
}

static void mel__text_pass_push_quad(Mel_Text_Pass* pass, Mel_Text_Entry* entry)
{
    if (pass->vertex_count / 4 >= pass->max_glyphs)
    {
        SDL_Log("Text pass overflow!");
        return;
    }

    Mel_Text_Vertex* vertices = pass->vertices;
    u32 vi = pass->vertex_count;
    u32 ii = pass->index_count;

    f32 x = entry->pos.x;
    f32 y = entry->pos.y;
    f32 w = entry->size.x;
    f32 h = entry->size.y;
    f32 u0 = entry->uv.x;
    f32 v0 = entry->uv.y;
    f32 u1 = entry->uv.x + entry->uv.w;
    f32 v1 = entry->uv.y + entry->uv.h;

    Mel_Text_Vertex v = {
        .r = entry->color.x, .g = entry->color.y, .b = entry->color.z, .a = entry->color.w,
        .or_ = entry->outline_color.x, .og = entry->outline_color.y, .ob = entry->outline_color.z, .oa = entry->outline_color.w,
        .mode = (f32)entry->mode, .edge = entry->edge, .softness = entry->softness, .outline = entry->outline,
        .px_range = entry->px_range,
    };

    vertices[vi + 0] = (Mel_Text_Vertex){ .x = x,     .y = y,     .u = u0, .v = v0, .r = v.r, .g = v.g, .b = v.b, .a = v.a, .or_ = v.or_, .og = v.og, .ob = v.ob, .oa = v.oa, .mode = v.mode, .edge = v.edge, .softness = v.softness, .outline = v.outline, .px_range = v.px_range };
    vertices[vi + 1] = (Mel_Text_Vertex){ .x = x + w, .y = y,     .u = u1, .v = v0, .r = v.r, .g = v.g, .b = v.b, .a = v.a, .or_ = v.or_, .og = v.og, .ob = v.ob, .oa = v.oa, .mode = v.mode, .edge = v.edge, .softness = v.softness, .outline = v.outline, .px_range = v.px_range };
    vertices[vi + 2] = (Mel_Text_Vertex){ .x = x + w, .y = y + h, .u = u1, .v = v1, .r = v.r, .g = v.g, .b = v.b, .a = v.a, .or_ = v.or_, .og = v.og, .ob = v.ob, .oa = v.oa, .mode = v.mode, .edge = v.edge, .softness = v.softness, .outline = v.outline, .px_range = v.px_range };
    vertices[vi + 3] = (Mel_Text_Vertex){ .x = x,     .y = y + h, .u = u0, .v = v1, .r = v.r, .g = v.g, .b = v.b, .a = v.a, .or_ = v.or_, .og = v.og, .ob = v.ob, .oa = v.oa, .mode = v.mode, .edge = v.edge, .softness = v.softness, .outline = v.outline, .px_range = v.px_range };

    pass->indices[ii + 0] = (u16)(vi + 0);
    pass->indices[ii + 1] = (u16)(vi + 1);
    pass->indices[ii + 2] = (u16)(vi + 2);
    pass->indices[ii + 3] = (u16)(vi + 2);
    pass->indices[ii + 4] = (u16)(vi + 3);
    pass->indices[ii + 5] = (u16)(vi + 0);

    pass->vertex_count += 4;
    pass->index_count += 6;
}

static void mel__text_pass_begin(Mel_Text_Pass* pass, u32 frame_index)
{
    assert(frame_index < MEL_MAX_FRAMES_IN_FLIGHT);
    pass->gpu_frame_index = frame_index;
    pass->vertices = pass->gpu_frames[frame_index].vertex_buffer.mapped;
    pass->indices = (u16*)pass->gpu_frames[frame_index].index_buffer.mapped;
    pass->vertex_count = 0;
    pass->index_count = 0;
    pass->draw_count = 0;
    pass->current_texture = nullptr;
    pass->_current_descriptor = nullptr;
    mel__text_pass_set_texture(pass, &pass->white_texture);
}

static void mel__text_pass_flush(Mel_Text_Pass* pass, Mel_Gpu_Cmd* cmd, Mel_Mat4* projection)
{
    if (pass->draw_count > 0)
    {
        Mel_Text_Draw_Cmd* last = &pass->draws[pass->draw_count - 1];
        last->index_count = pass->index_count - last->index_offset;
    }

    if (pass->index_count == 0)
        return;

    Mel_Text_Gpu_Frame* frame = &pass->gpu_frames[pass->gpu_frame_index];
    mel_gpu_buffer_flush(&frame->vertex_buffer, cmd->dev);
    mel_gpu_buffer_flush(&frame->index_buffer, cmd->dev);

    mel_gpu_pipeline_bind(&pass->pipeline, cmd);
    mel_gpu_cmd_push_constants(cmd, &pass->pipeline, MEL_GPU_SHADER_STAGE_VERTEX,
        0, sizeof(Mel_Mat4), projection);

    mel_gpu_cmd_bind_vertex_buffer(cmd, &frame->vertex_buffer, 0);
    mel_gpu_cmd_bind_index_buffer(cmd, &frame->index_buffer, 0, MEL_GPU_INDEX_TYPE_U16);

    for (u32 i = 0; i < pass->draw_count; i++)
    {
        Mel_Text_Draw_Cmd* draw = &pass->draws[i];
        if (draw->index_count == 0) continue;
        mel_gpu_cmd_bind_descriptor_set(cmd, &pass->pipeline, draw->_descriptor);
        mel_gpu_cmd_draw_indexed(cmd, draw->index_count, 1, draw->index_offset, 0, 0);
    }
}

bool mel_text_pass_init_opt(Mel_Text_Pass* pass, Mel_Text_Pass_Init_Opt opt)
{
    assert(pass != nullptr);
    assert(opt.dev != nullptr);

    *pass = (Mel_Text_Pass){0};
    pass->dev = opt.dev;
    pass->alloc = opt.alloc ? opt.alloc : mel_alloc_heap();

    mel_gpu_shader_init(&pass->shader, opt.dev, .source = str8_from_cstr(TEXT_SHADER_SOURCE));

    Mel_Gpu_Vertex_Binding binding = {
        .binding = 0,
        .stride = sizeof(Mel_Text_Vertex),
        .input_rate = 0,
    };

    Mel_Gpu_Vertex_Attribute attributes[] = {
        { .location = 0, .binding = 0, .format = MEL_GPU_FORMAT_R32G32_SFLOAT, .offset = offsetof(Mel_Text_Vertex, x) },
        { .location = 1, .binding = 0, .format = MEL_GPU_FORMAT_R32G32_SFLOAT, .offset = offsetof(Mel_Text_Vertex, u) },
        { .location = 2, .binding = 0, .format = MEL_GPU_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(Mel_Text_Vertex, r) },
        { .location = 3, .binding = 0, .format = MEL_GPU_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(Mel_Text_Vertex, or_) },
        { .location = 4, .binding = 0, .format = MEL_GPU_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(Mel_Text_Vertex, mode) },
        { .location = 5, .binding = 0, .format = MEL_GPU_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(Mel_Text_Vertex, px_range) },
    };

    mel_gpu_pipeline_init(&pass->pipeline, opt.dev,
        .shader = &pass->shader,
        .color_format = opt.color_format,
        .bindings = &binding,
        .binding_count = 1,
        .attributes = attributes,
        .attribute_count = 6,
        .push_constant_size = sizeof(Mel_Mat4),
        .use_texture = true,
        .blend_mode = MEL_GPU_BLEND_ALPHA);

    pass->max_glyphs = opt.max_glyphs > 0 ? opt.max_glyphs : 4096;
    u32 vertex_size = pass->max_glyphs * 4 * sizeof(Mel_Text_Vertex);
    u32 index_size = pass->max_glyphs * 6 * sizeof(u16);

    for (u32 i = 0; i < MEL_MAX_FRAMES_IN_FLIGHT; i++)
    {
        mel_gpu_buffer_init(&pass->gpu_frames[i].vertex_buffer, opt.dev,
            .size = vertex_size,
            .usage = MEL_GPU_BUFFER_USAGE_VERTEX,
            .memory_usage = MEL_GPU_MEMORY_USAGE_CPU_TO_GPU);
        mel_gpu_buffer_init(&pass->gpu_frames[i].index_buffer, opt.dev,
            .size = index_size,
            .usage = MEL_GPU_BUFFER_USAGE_INDEX,
            .memory_usage = MEL_GPU_MEMORY_USAGE_CPU_TO_GPU);
    }

    pass->gpu_frame_index = 0;
    pass->vertices = pass->gpu_frames[0].vertex_buffer.mapped;
    pass->indices = (u16*)pass->gpu_frames[0].index_buffer.mapped;

    mel_gpu_texture_init_white(&pass->white_texture, opt.dev);
    return true;
}

void mel_text_pass_shutdown(Mel_Text_Pass* pass)
{
    assert(pass != nullptr);
    assert(pass->dev != nullptr);

    if (pass->descriptor_cache)
        mel_dealloc(pass->alloc, pass->descriptor_cache);
    if (pass->draws)
        mel_dealloc(pass->alloc, pass->draws);

    for (u32 i = 0; i < MEL_MAX_FRAMES_IN_FLIGHT; i++)
    {
        mel_gpu_buffer_shutdown(&pass->gpu_frames[i].index_buffer, pass->dev);
        mel_gpu_buffer_shutdown(&pass->gpu_frames[i].vertex_buffer, pass->dev);
    }
    mel_gpu_texture_shutdown(&pass->white_texture, pass->dev);
    mel_gpu_pipeline_shutdown(&pass->pipeline, pass->dev);
    mel_gpu_shader_shutdown(&pass->shader, pass->dev);
    pass->dev = nullptr;
}

static void mel__text_pass_draw_list(Mel_Render_List* list, Mel_Text_Pass* pass)
{
    if (list->dirty)
        mel_render_list_sort(list);

    for (u32 i = 0; i < list->count; i++)
    {
        Mel_Text_Entry* entry = mel_render_list_get(list, list->packets[i].entry_index);
        mel__text_pass_set_texture(pass, entry->texture);
        mel__text_pass_push_quad(pass, entry);
    }
}

void mel_text_pass_execute(Mel_Render_Pass_Ctx* ctx)
{
    Mel_Text_Pass* pass = ctx->user;
    assert(pass != nullptr);

    Mel_Mat4 vp = mel_camera_vp(ctx->camera);
    mel__text_pass_begin(pass, ctx->gpu_frame_index);

    if (ctx->read_lists)
    {
        for (u32 i = 0; ctx->read_lists[i] != nullptr; i++)
            mel__text_pass_draw_list(ctx->read_lists[i], pass);
    }

    mel__text_pass_flush(pass, &ctx->cmd, &vp);
}
