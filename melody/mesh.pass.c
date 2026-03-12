#include "mesh.pass.h"
#include "render.list.h"
#include "render.pass.h"
#include "render.camera.h"
#include "render.source.h"
#include "render.material.h"
#include "allocator.heap.h"
#include "string.str8.h"

#include <SDL3/SDL.h>

typedef struct {
    f32 x, y, z;
    f32 nx, ny, nz;
    f32 r, g, b, a;
    u32 material_id;
} Mel_Mesh_Vertex;

typedef struct {
    Mel_Gpu_Pipeline* pipeline;
    VkBuffer buffer;
    VkDescriptorSet descriptor;
} Mel__Mesh_Descriptor_Cache_Entry;

static const char* MESH_FORWARD_SHADER_SOURCE =
"struct VSInput\n"
"{\n"
"    float3 position : POSITION;\n"
"    float3 normal : NORMAL;\n"
"    float4 color : COLOR0;\n"
"    uint materialId : TEXCOORD0;\n"
"};\n"
"\n"
"struct MaterialRecord\n"
"{\n"
"    float4 baseColor;\n"
"    float4 params0;\n"
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
"    float4 lightDirAmbient;\n"
"    float4 lightColor;\n"
"};\n"
"\n"
"[[vk::push_constant]] PushConstants push;\n"
"[[vk::binding(0, 0)]] StructuredBuffer<MaterialRecord> gMaterials;\n"
"\n"
"[shader(\"vertex\")]\n"
"VSOutput vertexMain(VSInput input)\n"
"{\n"
"    VSOutput output;\n"
"    MaterialRecord material = gMaterials[input.materialId];\n"
"    float lightingWeight = saturate(material.params0.z);\n"
"    float roughness = saturate(material.params0.x);\n"
"    float metallic = saturate(material.params0.y);\n"
"    float4 shadedColor = input.color * material.baseColor;\n"
"    float3 normal = normalize(input.normal);\n"
"    float3 lightDir = normalize(-push.lightDirAmbient.xyz);\n"
"    float ndotl = saturate(dot(normal, lightDir));\n"
"    float diffuseStrength = lerp(1.0, 0.35, roughness) * lerp(1.0, 0.70, metallic);\n"
"    float specPower = lerp(32.0, 6.0, roughness);\n"
"    float specular = pow(max(ndotl, 0.0001), specPower) * metallic;\n"
"    float3 lit = shadedColor.rgb * push.lightDirAmbient.www;\n"
"    lit += shadedColor.rgb * (push.lightColor.rgb * ndotl * diffuseStrength);\n"
"    lit += push.lightColor.rgb * specular;\n"
"    output.position = mul(push.projection, float4(input.position, 1.0));\n"
"    output.color = shadedColor;\n"
"    output.color.rgb = lerp(shadedColor.rgb, lit, lightingWeight);\n"
"    return output;\n"
"}\n"
"\n"
"[shader(\"fragment\")]\n"
"float4 fragmentMain(VSOutput input) : SV_Target\n"
"{\n"
"    return input.color;\n"
"}\n";

static const char* MESH_INDIRECT_COMPUTE_SHADER_SOURCE =
"struct CullPushConstants\n"
"{\n"
"    float4x4 viewProjection;\n"
"    float4 bounds; // xyz center, w radius\n"
"    uint indexCount;\n"
"};\n"
"\n"
"struct DrawIndexedIndirectCommand\n"
"{\n"
"    uint indexCount;\n"
"    uint instanceCount;\n"
"    uint firstIndex;\n"
"    int vertexOffset;\n"
"    uint firstInstance;\n"
"};\n"
"\n"
"[[vk::push_constant]] CullPushConstants push;\n"
"[[vk::binding(0, 0)]] RWStructuredBuffer<DrawIndexedIndirectCommand> gIndirect;\n"
"\n"
"[shader(\"compute\")]\n"
"[numthreads(1, 1, 1)]\n"
"void computeMain(uint3 dispatchThreadID : SV_DispatchThreadID)\n"
"{\n"
"    float4 clip = mul(push.viewProjection, float4(push.bounds.xyz, 1.0));\n"
"    float radius = abs(push.bounds.w);\n"
"    float w = max(abs(clip.w), 0.0001);\n"
"    bool visible = clip.z >= -w - radius && clip.z <= w + radius;\n"
"    visible = visible && abs(clip.x) <= w + radius;\n"
"    visible = visible && abs(clip.y) <= w + radius;\n"
"    DrawIndexedIndirectCommand cmd;\n"
"    cmd.indexCount = visible ? push.indexCount : 0;\n"
"    cmd.instanceCount = visible ? 1 : 0;\n"
"    cmd.firstIndex = 0;\n"
"    cmd.vertexOffset = 0;\n"
"    cmd.firstInstance = 0;\n"
"    gIndirect[0] = cmd;\n"
"}\n";

static const char* MESH_MESH_SHADER_SOURCE =
"struct VertexInput\n"
"{\n"
"    float3 position;\n"
"    float3 normal;\n"
"    float4 color;\n"
"    uint materialId;\n"
"};\n"
"\n"
"struct MaterialRecord\n"
"{\n"
"    float4 baseColor;\n"
"    float4 params0;\n"
"};\n"
"\n"
"struct MeshOutput\n"
"{\n"
"    float4 position : SV_Position;\n"
"    float4 color : COLOR0;\n"
"};\n"
"\n"
"struct PushConstants\n"
"{\n"
"    float4x4 projection;\n"
"    float4 lightDirAmbient;\n"
"    float4 lightColor;\n"
"    uint vertexCount;\n"
"    uint indexCount;\n"
"};\n"
"\n"
"[[vk::push_constant]] PushConstants push;\n"
"[[vk::binding(0, 0)]] StructuredBuffer<VertexInput> gVertices;\n"
"[[vk::binding(1, 0)]] StructuredBuffer<uint> gIndices;\n"
"[[vk::binding(2, 0)]] StructuredBuffer<MaterialRecord> gMaterials;\n"
"\n"
"[shader(\"mesh\")]\n"
"[outputtopology(\"triangle\")]\n"
"[numthreads(32, 1, 1)]\n"
"void meshMain(uint tid : SV_GroupThreadID,\n"
"    out vertices MeshOutput verts[64],\n"
"    out indices uint3 tris[42])\n"
"{\n"
"    uint vertexCount = min(push.vertexCount, 64u);\n"
"    uint primitiveCount = min(push.indexCount / 3u, 42u);\n"
"    SetMeshOutputCounts(vertexCount, primitiveCount);\n"
"    if (tid < vertexCount)\n"
"    {\n"
"        VertexInput input = gVertices[tid];\n"
"        MaterialRecord material = gMaterials[input.materialId];\n"
"        float lightingWeight = saturate(material.params0.z);\n"
"        float roughness = saturate(material.params0.x);\n"
"        float metallic = saturate(material.params0.y);\n"
"        float4 shadedColor = input.color * material.baseColor;\n"
"        float3 normal = normalize(input.normal);\n"
"        float3 lightDir = normalize(-push.lightDirAmbient.xyz);\n"
"        float ndotl = saturate(dot(normal, lightDir));\n"
"        float diffuseStrength = lerp(1.0, 0.35, roughness) * lerp(1.0, 0.70, metallic);\n"
"        float specPower = lerp(32.0, 6.0, roughness);\n"
"        float specular = pow(max(ndotl, 0.0001), specPower) * metallic;\n"
"        float3 lit = shadedColor.rgb * push.lightDirAmbient.www;\n"
"        lit += shadedColor.rgb * (push.lightColor.rgb * ndotl * diffuseStrength);\n"
"        lit += push.lightColor.rgb * specular;\n"
"        verts[tid].position = mul(push.projection, float4(input.position, 1.0));\n"
"        verts[tid].color = shadedColor;\n"
"        verts[tid].color.rgb = lerp(shadedColor.rgb, lit, lightingWeight);\n"
"    }\n"
"    if (tid < primitiveCount)\n"
"    {\n"
"        uint baseIndex = tid * 3u;\n"
"        tris[tid] = uint3(gIndices[baseIndex], gIndices[baseIndex + 1], gIndices[baseIndex + 2]);\n"
"    }\n"
"}\n"
"\n"
"[shader(\"fragment\")]\n"
"float4 fragmentMain(MeshOutput input) : SV_Target\n"
"{\n"
"    return input.color;\n"
"}\n";

typedef struct {
    Mel_Mat4 projection;
    Mel_Vec4 light_dir_ambient;
    Mel_Vec4 light_color;
} Mel_Mesh_Push_Constants;

typedef struct {
    Mel_Mat4 view_projection;
    Mel_Vec4 bounds;
    u32 index_count;
} Mel_Mesh_Indirect_Cull_Push_Constants;

static Mel_Vec3 mel__mesh_pass_safe_normalize(Mel_Vec3 v, Mel_Vec3 fallback)
{
    f32 len_sq = mel_vec3_len_sq(v);
    if (len_sq <= 1e-8f)
        return fallback;
    return mel_vec3_scale(v, 1.0f / __builtin_sqrtf(len_sq));
}

static Mel_Material_Gpu_Record mel__mesh_pass_default_material_record(void)
{
    return (Mel_Material_Gpu_Record){
        .base_color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
        .params0 = mel_vec4(0.45f, 0.0f, 0.0f, 0.0f),
    };
}

static u32 mel__mesh_pass_push_material(Mel_Mesh_Pass* pass, Mel_Material_Instance_Handle material)
{
    assert(pass->material_count < pass->max_materials);
    u32 index = pass->material_count++;
    pass->material_records[index] = mel_material_instance_handle_valid(material)
        ? mel_material_instance_pack_gpu_record(material)
        : mel__mesh_pass_default_material_record();
    return index;
}

static VkDescriptorSet mel__mesh_pass_descriptor_for_buffer(Mel_Mesh_Pass* pass, Mel_Gpu_Pipeline* pipeline, VkBuffer buffer)
{
    for (u32 i = 0; i < pass->descriptor_cache_count; i++)
    {
        Mel__Mesh_Descriptor_Cache_Entry* entry = &((Mel__Mesh_Descriptor_Cache_Entry*)pass->descriptor_cache)[i];
        if (entry->pipeline == pipeline && entry->buffer == buffer)
            return entry->descriptor;
    }

    if (pass->descriptor_cache_count >= pass->descriptor_cache_capacity)
    {
        u32 new_capacity = pass->descriptor_cache_capacity == 0 ? 8 : pass->descriptor_cache_capacity * 2;
        usize size = sizeof(Mel__Mesh_Descriptor_Cache_Entry) * new_capacity;
        pass->descriptor_cache = pass->descriptor_cache
            ? mel_realloc(pass->alloc, pass->descriptor_cache, size)
            : mel_alloc(pass->alloc, size);
        pass->descriptor_cache_capacity = new_capacity;
    }

    VkDescriptorSet descriptor = mel_gpu_pipeline_alloc_descriptor(pipeline, pass->dev);
    mel_gpu_pipeline_write_buffer_binding(pipeline, pass->dev, descriptor, 0,
        buffer, 0, VK_WHOLE_SIZE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
    ((Mel__Mesh_Descriptor_Cache_Entry*)pass->descriptor_cache)[pass->descriptor_cache_count++] =
        (Mel__Mesh_Descriptor_Cache_Entry){ .pipeline = pipeline, .buffer = buffer, .descriptor = descriptor };
    return descriptor;
}

static void mel__mesh_pass_begin(Mel_Mesh_Pass* pass)
{
    pass->gpu_frame_index = (pass->gpu_frame_index + 1) % MEL_MAX_FRAMES_IN_FLIGHT;
    pass->vertices = pass->gpu_frames[pass->gpu_frame_index].vertex_buffer.mapped;
    pass->indices = (u32*)pass->gpu_frames[pass->gpu_frame_index].index_buffer.mapped;
    pass->material_records = pass->gpu_frames[pass->gpu_frame_index].material_buffer.mapped;
    pass->vertex_count = 0;
    pass->index_count = 0;
    pass->material_count = 0;
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
    u32 material_id = mel__mesh_pass_push_material(pass, entry->material);

    for (u32 i = 0; i < entry->mesh->vertex_count; i++)
    {
        Mel_Vec3 p = mel_mat4_mul_point(entry->transform, entry->mesh->positions[i]);
        Mel_Vec3 n = entry->mesh->normals
            ? mel_mat4_mul_dir(entry->transform, entry->mesh->normals[i])
            : entry->mesh->positions[i];
        n = mel__mesh_pass_safe_normalize(n, mel_vec3(0.0f, 0.0f, 1.0f));
        Mel_Vec4 color = entry->mesh->colors ? entry->mesh->colors[i] : entry->color;
        vertices[pass->vertex_count++] = (Mel_Mesh_Vertex){
            .x = p.x,
            .y = p.y,
            .z = p.z,
            .nx = n.x,
            .ny = n.y,
            .nz = n.z,
            .r = color.x,
            .g = color.y,
            .b = color.z,
            .a = color.w,
            .material_id = material_id,
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

static void mel__mesh_pass_bind(Mel_Mesh_Pass* pass, Mel_Gpu_Cmd cmd, const Mel_Mat4* projection, VkBuffer material_buffer)
{
    Mel_Mesh_Push_Constants push = {
        .projection = *projection,
        .light_dir_ambient = mel_vec4(pass->lighting.direction.x, pass->lighting.direction.y, pass->lighting.direction.z, pass->lighting.ambient),
        .light_color = pass->lighting.color,
    };
    mel_gpu_pipeline_bind(&pass->pipeline, cmd.cmd);
    VkDescriptorSet descriptor = mel__mesh_pass_descriptor_for_buffer(pass, material_buffer);
    vkCmdBindDescriptorSets(cmd.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pass->pipeline.layout, 0, 1, &descriptor, 0, nullptr);
    vkCmdPushConstants(cmd.cmd, pass->pipeline.layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push), &push);
}

static void mel__mesh_pass_flush(Mel_Mesh_Pass* pass, Mel_Gpu_Cmd cmd)
{
    if (pass->index_count == 0)
        return;

    Mel_Mesh_Gpu_Frame* frame = &pass->gpu_frames[pass->gpu_frame_index];
    mel_gpu_buffer_flush(&frame->vertex_buffer, cmd.dev);
    mel_gpu_buffer_flush(&frame->index_buffer, cmd.dev);
    mel_gpu_buffer_flush(&frame->material_buffer, cmd.dev);

    VkBuffer buffers[] = { frame->vertex_buffer.buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd.cmd, 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(cmd.cmd, frame->index_buffer.buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd.cmd, pass->index_count, 1, 0, 0, 0);
}

static void mel__mesh_pass_draw_stream(Mel_Gpu_Cmd cmd, const Mel_Mesh_Gpu_Draw_Stream* stream)
{
    if (!stream || stream->vertex_buffer == VK_NULL_HANDLE || stream->index_buffer == VK_NULL_HANDLE || stream->index_count == 0)
        return;

    VkBuffer buffers[] = { stream->vertex_buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd.cmd, 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(cmd.cmd, stream->index_buffer, 0, VK_INDEX_TYPE_UINT32);
    vkCmdDrawIndexed(cmd.cmd, stream->index_count, 1, 0, 0, 0);
}

static void mel__mesh_pass_draw_indirect_stream(Mel_Gpu_Cmd cmd, const Mel_Mesh_Gpu_Indirect_Stream* stream)
{
    if (!stream ||
        stream->vertex_buffer == VK_NULL_HANDLE ||
        stream->index_buffer == VK_NULL_HANDLE ||
        stream->indirect_buffer == VK_NULL_HANDLE ||
        stream->draw_count == 0)
        return;

    VkBuffer buffers[] = { stream->vertex_buffer };
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd.cmd, 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(cmd.cmd, stream->index_buffer, 0, VK_INDEX_TYPE_UINT32);
    mel_gpu_cmd_draw_indexed_indirect(&cmd, stream->indirect_buffer, 0, stream->draw_count,
        stream->stride ? stream->stride : sizeof(VkDrawIndexedIndirectCommand));
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
    pass->lighting = (Mel_Mesh_Lighting){
        .direction = mel__mesh_pass_safe_normalize(mel_vec3(0.5f, -1.0f, 0.35f), mel_vec3(0.0f, -1.0f, 0.0f)),
        .color = mel_vec4(0.95f, 0.97f, 1.0f, 1.0f),
        .ambient = 0.18f,
    };

    mel_gpu_shader_init(&pass->shader, opt.dev, .source = str8_from_cstr(MESH_SHADER_SOURCE));

    VkVertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(Mel_Mesh_Vertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    VkVertexInputAttributeDescription attributes[] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Mel_Mesh_Vertex, x) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Mel_Mesh_Vertex, nx) },
        { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(Mel_Mesh_Vertex, r) },
        { .location = 3, .binding = 0, .format = VK_FORMAT_R32_UINT, .offset = offsetof(Mel_Mesh_Vertex, material_id) },
    };

    mel_gpu_pipeline_init(&pass->pipeline, opt.dev,
        .shader = &pass->shader,
        .color_format = opt.color_format,
        .depth_format = opt.depth_format,
        .bindings = &binding,
        .binding_count = 1,
        .attributes = attributes,
        .attribute_count = 4,
        .cull_mode = MEL_GPU_CULL_BACK,
        .depth_test = true,
        .depth_write = true,
        .push_constant_size = sizeof(Mel_Mesh_Push_Constants),
        .descriptor_bindings = (Mel_Gpu_Descriptor_Binding[]){
            { .binding = 0, .type = MEL_GPU_DESCRIPTOR_STORAGE_BUFFER, .count = 1, .stages = VK_SHADER_STAGE_VERTEX_BIT },
        },
        .descriptor_binding_count = 1);

    u32 vertex_bytes = pass->max_vertices * sizeof(Mel_Mesh_Vertex);
    u32 index_bytes = pass->max_indices * sizeof(u32);
    pass->max_materials = pass->max_vertices;
    u32 material_bytes = pass->max_materials * sizeof(Mel_Material_Gpu_Record);

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
        mel_gpu_buffer_init(&pass->gpu_frames[i].material_buffer, opt.dev,
            .size = material_bytes,
            .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            .memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU,
            .map_on_create = true);
    }

    pass->vertices = pass->gpu_frames[0].vertex_buffer.mapped;
    pass->indices = (u32*)pass->gpu_frames[0].index_buffer.mapped;
    pass->material_records = pass->gpu_frames[0].material_buffer.mapped;
    return true;
}

void mel_mesh_pass_set_lighting(Mel_Mesh_Pass* pass, Mel_Mesh_Lighting lighting)
{
    assert(pass != nullptr);
    pass->lighting = lighting;
    pass->lighting.direction = mel__mesh_pass_safe_normalize(lighting.direction, mel_vec3(0.0f, -1.0f, 0.0f));
}

Mel_Mesh_Lighting mel_mesh_pass_lighting(Mel_Mesh_Pass* pass)
{
    assert(pass != nullptr);
    return pass->lighting;
}

void mel_mesh_pass_shutdown(Mel_Mesh_Pass* pass)
{
    assert(pass != nullptr);
    assert(pass->dev != nullptr);

    for (u32 i = 0; i < MEL_MAX_FRAMES_IN_FLIGHT; i++)
    {
        mel_gpu_buffer_shutdown(&pass->gpu_frames[i].material_buffer, pass->dev);
        mel_gpu_buffer_shutdown(&pass->gpu_frames[i].index_buffer, pass->dev);
        mel_gpu_buffer_shutdown(&pass->gpu_frames[i].vertex_buffer, pass->dev);
    }
    mel_gpu_pipeline_shutdown(&pass->pipeline, pass->dev);
    mel_gpu_shader_shutdown(&pass->shader, pass->dev);
    if (pass->descriptor_cache)
        mel_dealloc(pass->alloc, pass->descriptor_cache);
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

    bool has_gpu_streams = false;
    bool has_gpu_indirect_streams = false;
    Mel_Gpu_Buffer* external_material_buffer = nullptr;
    if (ctx->read_sources)
    {
        for (u32 i = 0; mel_source_handle_valid(ctx->read_sources[i]); i++)
        {
            Mel_Source_Handle source = ctx->read_sources[i];
            if (mel_source_kind(source) == MEL_SOURCE_GPU_BUFFER &&
                mel_source_schema(source) == MEL_SCHEMA_MESH_DRAW_STREAM &&
                mel_source_user(source) != nullptr)
            {
                has_gpu_streams = true;
            }
            if (mel_source_kind(source) == MEL_SOURCE_GPU_BUFFER &&
                mel_source_schema(source) == MEL_SCHEMA_MESH_INDIRECT_STREAM &&
                mel_source_user(source) != nullptr)
            {
                has_gpu_indirect_streams = true;
            }
            if (mel_source_kind(source) == MEL_SOURCE_GPU_BUFFER &&
                mel_source_schema(source) == MEL_SCHEMA_MATERIAL_TABLE)
            {
                external_material_buffer = mel_source_gpu_buffer(source);
            }
        }
    }

    if ((has_gpu_streams || has_gpu_indirect_streams) && !external_material_buffer && pass->material_count == 0)
        mel__mesh_pass_push_material(pass, MEL_MATERIAL_INSTANCE_HANDLE_NULL);

    VkBuffer material_buffer = external_material_buffer
        ? external_material_buffer->buffer
        : pass->gpu_frames[pass->gpu_frame_index].material_buffer.buffer;

    if (!external_material_buffer && pass->material_count > 0)
        mel_gpu_buffer_flush(&pass->gpu_frames[pass->gpu_frame_index].material_buffer, ctx->cmd.dev);

    if (pass->index_count > 0 || has_gpu_streams || has_gpu_indirect_streams)
        mel__mesh_pass_bind(pass, ctx->cmd, &vp, material_buffer);

    mel__mesh_pass_flush(pass, ctx->cmd);

    if (ctx->read_sources)
    {
        for (u32 i = 0; mel_source_handle_valid(ctx->read_sources[i]); i++)
        {
            Mel_Source_Handle source = ctx->read_sources[i];
            if (mel_source_kind(source) != MEL_SOURCE_GPU_BUFFER ||
                (mel_source_schema(source) != MEL_SCHEMA_MESH_DRAW_STREAM &&
                 mel_source_schema(source) != MEL_SCHEMA_MESH_INDIRECT_STREAM))
                continue;

            if (mel_source_schema(source) == MEL_SCHEMA_MESH_DRAW_STREAM)
            {
                const Mel_Mesh_Gpu_Draw_Stream* stream = mel_source_user(source);
                mel__mesh_pass_draw_stream(ctx->cmd, stream);
            }
            else
            {
                const Mel_Mesh_Gpu_Indirect_Stream* stream = mel_source_user(source);
                mel__mesh_pass_draw_indirect_stream(ctx->cmd, stream);
            }
        }
    }
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
        .material = opt.material,
    };
}
