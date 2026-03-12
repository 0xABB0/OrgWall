#include "mesh.pass.h"
#include "render.list.h"
#include "render.pass.h"
#include "render.camera.h"
#include "allocator.heap.h"
#include "string.str8.h"

#include <SDL3/SDL.h>

typedef struct {
    f32 x, y, z;
    f32 r, g, b, a;
} Mel_Mesh_Vertex;

static const char* MESH_SHADER_SOURCE =
"struct VSInput\n"
"{\n"
"    float3 position : POSITION;\n"
"    float4 color : COLOR0;\n"
"};\n"
"\n"
"struct VSOutput\n"
"{\n"
"    float4 position : SV_Position;\n"
"    float4 color : COLOR0;\n"
"};\n"
"\n"
"struct PushConstants\n"
"{\n"
"    float4x4 projection;\n"
"};\n"
"\n"
"[[vk::push_constant]] PushConstants push;\n"
"\n"
"[shader(\"vertex\")]\n"
"VSOutput vertexMain(VSInput input)\n"
"{\n"
"    VSOutput output;\n"
"    output.position = mul(push.projection, float4(input.position, 1.0));\n"
"    output.color = input.color;\n"
"    return output;\n"
"}\n"
"\n"
"[shader(\"fragment\")]\n"
"float4 fragmentMain(VSOutput input) : SV_Target\n"
"{\n"
"    return input.color;\n"
"}\n";

static void mel__mesh_pass_begin(Mel_Mesh_Pass* pass)
{
    pass->gpu_frame_index = (pass->gpu_frame_index + 1) % MEL_MAX_FRAMES_IN_FLIGHT;
    pass->vertices = pass->gpu_frames[pass->gpu_frame_index].vertex_buffer.mapped;
    pass->indices = (u32*)pass->gpu_frames[pass->gpu_frame_index].index_buffer.mapped;
    pass->vertex_count = 0;
    pass->index_count = 0;
}

static bool mel__mesh_pass_push_mesh(Mel_Mesh_Pass* pass, const Mel_Mesh_Entry* entry)
{
    if (!entry->mesh || !entry->mesh->positions || !entry->mesh->indices)
        return true;

    if (pass->vertex_count + entry->mesh->vertex_count > pass->max_vertices ||
        pass->index_count + entry->mesh->index_count > pass->max_indices)
    {
        SDL_Log("Mesh pass overflow");
        return false;
    }

    Mel_Mesh_Vertex* vertices = pass->vertices;
    u32 base_vertex = pass->vertex_count;

    for (u32 i = 0; i < entry->mesh->vertex_count; i++)
    {
        Mel_Vec3 p = mel_mat4_mul_point(entry->transform, entry->mesh->positions[i]);
        Mel_Vec4 color = entry->mesh->colors ? entry->mesh->colors[i] : entry->color;
        vertices[pass->vertex_count++] = (Mel_Mesh_Vertex){
            .x = p.x,
            .y = p.y,
            .z = p.z,
            .r = color.x,
            .g = color.y,
            .b = color.z,
            .a = color.w,
        };
    }

    for (u32 i = 0; i < entry->mesh->index_count; i++)
        pass->indices[pass->index_count++] = base_vertex + entry->mesh->indices[i];

    return true;
}

static void mel__mesh_pass_draw_list(Mel_Render_List* list, Mel_Mesh_Pass* pass)
{
    if (list->dirty)
        mel_render_list_sort(list);

    for (u32 i = 0; i < list->count; i++)
    {
        Mel_Mesh_Entry* entry = mel_render_list_get(list, list->packets[i].entry_index);
        if (!mel__mesh_pass_push_mesh(pass, entry))
            return;
    }
}

static void mel__mesh_pass_flush(Mel_Mesh_Pass* pass, Mel_Gpu_Cmd cmd, const Mel_Mat4* projection)
{
    if (pass->index_count == 0)
        return;

    Mel_Mesh_Gpu_Frame* frame = &pass->gpu_frames[pass->gpu_frame_index];
    mel_gpu_buffer_flush(&frame->vertex_buffer, cmd.dev);
    mel_gpu_buffer_flush(&frame->index_buffer, cmd.dev);

    mel_gpu_pipeline_bind(&pass->pipeline, cmd.cmd);
    vkCmdPushConstants(cmd.cmd, pass->pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(Mel_Mat4), projection);

    VkBuffer buffers[] = { frame->vertex_buffer.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd.cmd, 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(cmd.cmd, frame->index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd.cmd, pass->index_count, 1, 0, 0, 0);
}

bool mel_mesh_pass_init_opt(Mel_Mesh_Pass* pass, Mel_Mesh_Pass_Init_Opt opt)
{
    assert(pass != nullptr);
    assert(opt.dev != nullptr);

    *pass = (Mel_Mesh_Pass){0};
    pass->dev = opt.dev;
    pass->alloc = opt.alloc ? opt.alloc : mel_alloc_heap();
    pass->max_vertices = opt.max_vertices > 0 ? opt.max_vertices : 65536;
    pass->max_indices = opt.max_indices > 0 ? opt.max_indices : 65536 * 3;

    mel_gpu_shader_init(&pass->shader, opt.dev, .source = str8_from_cstr(MESH_SHADER_SOURCE));

    VkVertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(Mel_Mesh_Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    VkVertexInputAttributeDescription attributes[] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Mel_Mesh_Vertex, x) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(Mel_Mesh_Vertex, r) },
    };

    mel_gpu_pipeline_init(&pass->pipeline, opt.dev,
        .shader = &pass->shader,
        .color_format = opt.color_format,
        .depth_format = opt.depth_format,
        .bindings = &binding,
        .binding_count = 1,
        .attributes = attributes,
        .attribute_count = 2,
        .cull_mode = MEL_GPU_CULL_BACK,
        .depth_test = true,
        .depth_write = true,
        .push_constant_size = sizeof(Mel_Mat4));

    u32 vertex_bytes = pass->max_vertices * sizeof(Mel_Mesh_Vertex);
    u32 index_bytes = pass->max_indices * sizeof(u32);

    for (u32 i = 0; i < MEL_MAX_FRAMES_IN_FLIGHT; i++)
    {
        mel_gpu_buffer_init(&pass->gpu_frames[i].vertex_buffer, opt.dev,
            .size = vertex_bytes,
            .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
            .memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU);
        mel_gpu_buffer_init(&pass->gpu_frames[i].index_buffer, opt.dev,
            .size = index_bytes,
            .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            .memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU);
    }

    pass->vertices = pass->gpu_frames[0].vertex_buffer.mapped;
    pass->indices = (u32*)pass->gpu_frames[0].index_buffer.mapped;
    return true;
}

void mel_mesh_pass_shutdown(Mel_Mesh_Pass* pass)
{
    assert(pass != nullptr);
    assert(pass->dev != nullptr);

    for (u32 i = 0; i < MEL_MAX_FRAMES_IN_FLIGHT; i++)
    {
        mel_gpu_buffer_shutdown(&pass->gpu_frames[i].index_buffer, pass->dev);
        mel_gpu_buffer_shutdown(&pass->gpu_frames[i].vertex_buffer, pass->dev);
    }
    mel_gpu_pipeline_shutdown(&pass->pipeline, pass->dev);
    mel_gpu_shader_shutdown(&pass->shader, pass->dev);
    pass->dev = nullptr;
}

void mel_mesh_pass_execute(Mel_Render_Pass_Ctx* ctx)
{
    Mel_Mesh_Pass* pass = ctx->user;
    assert(pass != nullptr);

    Mel_Mat4 vp = mel_camera_vp(ctx->camera);
    mel__mesh_pass_begin(pass);

    if (ctx->read_lists)
    {
        for (u32 i = 0; ctx->read_lists[i] != nullptr; i++)
            mel__mesh_pass_draw_list(ctx->read_lists[i], pass);
    }

    mel__mesh_pass_flush(pass, ctx->cmd, &vp);
}

void mel_draw_mesh_opt(Mel_Render_List* list, Mel_Draw_Mesh_Opt opt)
{
    assert(list != nullptr);
    assert(opt.mesh != nullptr);

    Mel_Mesh_Entry* entry = mel_render_list_push(list, mel_sort_key_mesh_opaque(opt.depth));
    *entry = (Mel_Mesh_Entry){
        .mesh = opt.mesh,
        .transform = opt.transform,
        .color = opt.color,
    };
}
