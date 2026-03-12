#pragma once

#include "mesh.pass.fwd.h"
#include "core.types.h"
#include "math.vec3.h"
#include "math.vec4.h"
#include "math.mat4.h"
#include "gpu.shader.h"
#include "gpu.pipeline.h"
#include "gpu.buffer.h"
#include "gpu.cmd.h"
#include "render.list.fwd.h"
#include "render.pass.fwd.h"
#include "render.material.h"
#include "allocator.fwd.h"

#ifndef MEL_MAX_FRAMES_IN_FLIGHT
#define MEL_MAX_FRAMES_IN_FLIGHT 3
#endif

struct Mel_Mesh {
    const Mel_Vec3* positions;
    const Mel_Vec3* normals;
    const Mel_Vec4* colors;
    u32 vertex_count;
    const u32* indices;
    u32 index_count;
};

struct Mel_Mesh_Entry {
    const Mel_Mesh* mesh;
    Mel_Mat4 transform;
    Mel_Vec4 color;
    Mel_Material_Instance_Handle material;
};
typedef struct Mel_Mesh_Entry Mel_Mesh_Entry;

typedef struct {
    VkBuffer vertex_buffer;
    VkBuffer index_buffer;
    u32 vertex_count;
    u32 index_count;
} Mel_Mesh_Gpu_Draw_Stream;

typedef struct {
    VkBuffer vertex_buffer;
    VkBuffer index_buffer;
    VkBuffer indirect_buffer;
    u32 vertex_count;
    u32 index_count;
    u32 draw_count;
    u32 stride;
} Mel_Mesh_Gpu_Indirect_Stream;

typedef struct {
    VkBuffer indirect_buffer;
    u32 index_count;
    Mel_Vec3 bounds_center;
    f32 bounds_radius;
} Mel_Mesh_Gpu_Cull_Stream;

typedef struct {
    Mel_Vec3 direction;
    Mel_Vec4 color;
    f32 ambient;
} Mel_Mesh_Lighting;

typedef struct {
    Mel_Gpu_Buffer vertex_buffer;
    Mel_Gpu_Buffer index_buffer;
    Mel_Gpu_Buffer material_buffer;
} Mel_Mesh_Gpu_Frame;

struct Mel_Mesh_Pass {
    Mel_Gpu_Shader shader;
    Mel_Gpu_Shader compute_shader;
    Mel_Gpu_Shader mesh_shader;
    Mel_Gpu_Pipeline pipeline;
    Mel_Gpu_Pipeline compute_pipeline;
    Mel_Gpu_Pipeline mesh_pipeline;
    Mel_Gpu_Device* dev;

    Mel_Mesh_Gpu_Frame gpu_frames[MEL_MAX_FRAMES_IN_FLIGHT];
    u32 gpu_frame_index;
    void* vertices;
    u32* indices;
    u32 vertex_count;
    u32 index_count;
    u32 material_count;
    u32 max_vertices;
    u32 max_indices;
    u32 max_materials;
    Mel_Material_Gpu_Record* material_records;
    void* descriptor_cache;
    u32 descriptor_cache_count;
    u32 descriptor_cache_capacity;

    Mel_Mesh_Lighting lighting;
    const Mel_Alloc* alloc;
};

typedef struct {
    Mel_Gpu_Device* dev;
    VkFormat color_format;
    VkFormat depth_format;
    u32 max_vertices;
    u32 max_indices;
    const Mel_Alloc* alloc;
} Mel_Mesh_Pass_Init_Opt;

bool mel_mesh_pass_init_opt(Mel_Mesh_Pass* pass, Mel_Mesh_Pass_Init_Opt opt);
#define mel_mesh_pass_init(pass, ...) mel_mesh_pass_init_opt((pass), (Mel_Mesh_Pass_Init_Opt){__VA_ARGS__})

void mel_mesh_pass_shutdown(Mel_Mesh_Pass* pass);

void mel_mesh_pass_set_lighting(Mel_Mesh_Pass* pass, Mel_Mesh_Lighting lighting);
Mel_Mesh_Lighting mel_mesh_pass_lighting(Mel_Mesh_Pass* pass);

void mel_mesh_pass_execute(Mel_Render_Pass_Ctx* ctx);

[[nodiscard]] static inline u64 mel_sort_key_mesh_opaque(f32 depth)
{
    u32 bits;
    __builtin_memcpy(&bits, &depth, sizeof(u32));
    u32 mask = (bits >> 31) ? 0xFFFFFFFFu : 0x80000000u;
    bits ^= mask;
    return (u64)bits;
}

typedef struct {
    const Mel_Mesh* mesh;
    Mel_Mat4 transform;
    Mel_Vec4 color;
    Mel_Material_Instance_Handle material;
    f32 depth;
} Mel_Draw_Mesh_Opt;

void mel_draw_mesh_opt(Mel_Render_List* list, Mel_Draw_Mesh_Opt opt);
#define mel_draw_mesh(list, ...) mel_draw_mesh_opt((list), (Mel_Draw_Mesh_Opt){__VA_ARGS__})
