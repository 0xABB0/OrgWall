#include <float.h>
#include <inttypes.h>
#include <math.h>
#include <SDL3/SDL.h>

#include <cimgui/cimgui.h>

#include "core.app.h"
#include "core.engine.h"
#include "window.h"
#include "swapchain.h"
#include "gpu.swapchain.h"
#include "gpu.buffer.h"
#include "gpu.device.h"
#include "vfs.h"
#include "string.str8.h"
#include "render.stage.3d.h"
#include "render.frame_plan.h"
#include "render.graph.h"
#include "render.view.h"
#include "render.list.h"
#include "render.camera.h"
#include "render.source.h"
#include "render.material.h"
#include "mesh.pass.h"
#include "sprite.pass.h"
#include "text.pass.h"
#include "text.draw.h"
#include "font.atlas.h"
#include "allocator.heap.h"
#include "math.mat4.h"
#include "math.vec2.h"
#include "math.vec3.h"
#include "math.vec4.h"
#include "sim.ctx.h"

#define WIN_W 1440
#define WIN_H 900
#define TAU_F 6.28318530718f

#define MATERIAL_VARIANT_COUNT 4
#define CHUNK_GRID_X 6
#define CHUNK_GRID_Z 6
#define CHUNK_COUNT (CHUNK_GRID_X * CHUNK_GRID_Z)
#define CHUNK_CUBES_X 6
#define CHUNK_CUBES_Z 6
#define CUBES_PER_CHUNK (CHUNK_CUBES_X * CHUNK_CUBES_Z)
#define TOTAL_CUBES (CHUNK_COUNT * CUBES_PER_CHUNK)

typedef enum {
    GPU_SCENE_MODE_LIST = 0,
    GPU_SCENE_MODE_DRAW_STREAM,
    GPU_SCENE_MODE_COMPUTE_INDIRECT,
    GPU_SCENE_MODE_AUTO,
    GPU_SCENE_MODE_COUNT,
} Gpu_Scene_Mode;

typedef enum {
    GPU_SCENE_POLICY_DEFAULT = 0,
    GPU_SCENE_POLICY_PERFORMANCE,
    GPU_SCENE_POLICY_PORTABLE,
    GPU_SCENE_POLICY_VISIBILITY,
    GPU_SCENE_POLICY_COUNT,
} Gpu_Scene_Policy;

typedef struct {
    f32 x, y, z;
    f32 nx, ny, nz;
    f32 r, g, b, a;
    u32 material_id;
} Scene_Stream_Vertex;

typedef struct {
    Mel_Gpu_Buffer vertex_buffer;
    Mel_Gpu_Buffer index_buffer;
    Mel_Mesh_Gpu_Draw_Stream draw_stream;
    Mel_Source_Handle draw_source;
    Mel_Vec3 bounds_center;
    Mel_Vec3 bounds_extent;
    f32 bounds_radius;
    u32 cube_count;
} Scene_Chunk;

typedef struct {
    Mel_Gpu_Buffer vertex_buffer;
    Mel_Gpu_Buffer index_buffer;
    Mel_Gpu_Buffer metadata_buffer;
    Mel_Gpu_Buffer indirect_buffer;
    Mel_Mesh_Gpu_Cull_Batch_Stream cull_stream;
    Mel_Source_Handle cull_source;
} Scene_Batch;

static Mel_Window_Handle s_window_handle;
static Mel_Swapchain_Handle s_swapchain_handle;
static Mel_Render_Stage_3D s_stage;
static Mel_Render_List s_world_meshes;
static Mel_Render_List s_hud_sprites;
static Mel_Render_List s_hud_text;
static Mel_Camera s_world_camera;
static Mel_Camera s_overlay_camera;
static Mel_Sim_Ctx s_sim;
static u8 s_event_buf[4096];
static Mel_Font_Handle s_font;
static Mel_Material_Template_Handle s_surface_template;
static Mel_Material_Instance_Handle s_materials[MATERIAL_VARIANT_COUNT];
static Mel_Material_Table s_material_table;
static Mel_Source_Handle s_mesh_list_source;
static Mel_Source_Handle s_material_table_source;
static Scene_Chunk s_chunks[CHUNK_COUNT];
static Scene_Batch s_batch;

static f32 s_time;
static f32 s_orbit_speed = 0.16f;
static f32 s_camera_distance = 42.0f;
static f32 s_camera_height = 18.0f;
static bool s_pause_animation;
static bool s_show_expected_visibility = true;
static Gpu_Scene_Mode s_mode = GPU_SCENE_MODE_AUTO;
static Gpu_Scene_Mode s_requested_mode = GPU_SCENE_MODE_AUTO;
static Gpu_Scene_Policy s_policy = GPU_SCENE_POLICY_DEFAULT;
static Gpu_Scene_Policy s_requested_policy = GPU_SCENE_POLICY_DEFAULT;
static u32 s_expected_visible_chunks;
static u32 s_expected_lod1_chunks;
static f32 s_lod_distance = 34.0f;

static const Mel_Vec3 s_cube_positions[] = {
    { .x = -1.0f, .y = -1.0f, .z =  1.0f },
    { .x =  1.0f, .y = -1.0f, .z =  1.0f },
    { .x =  1.0f, .y =  1.0f, .z =  1.0f },
    { .x = -1.0f, .y =  1.0f, .z =  1.0f },

    { .x =  1.0f, .y = -1.0f, .z = -1.0f },
    { .x = -1.0f, .y = -1.0f, .z = -1.0f },
    { .x = -1.0f, .y =  1.0f, .z = -1.0f },
    { .x =  1.0f, .y =  1.0f, .z = -1.0f },

    { .x = -1.0f, .y = -1.0f, .z = -1.0f },
    { .x = -1.0f, .y = -1.0f, .z =  1.0f },
    { .x = -1.0f, .y =  1.0f, .z =  1.0f },
    { .x = -1.0f, .y =  1.0f, .z = -1.0f },

    { .x =  1.0f, .y = -1.0f, .z =  1.0f },
    { .x =  1.0f, .y = -1.0f, .z = -1.0f },
    { .x =  1.0f, .y =  1.0f, .z = -1.0f },
    { .x =  1.0f, .y =  1.0f, .z =  1.0f },

    { .x = -1.0f, .y =  1.0f, .z =  1.0f },
    { .x =  1.0f, .y =  1.0f, .z =  1.0f },
    { .x =  1.0f, .y =  1.0f, .z = -1.0f },
    { .x = -1.0f, .y =  1.0f, .z = -1.0f },

    { .x = -1.0f, .y = -1.0f, .z = -1.0f },
    { .x =  1.0f, .y = -1.0f, .z = -1.0f },
    { .x =  1.0f, .y = -1.0f, .z =  1.0f },
    { .x = -1.0f, .y = -1.0f, .z =  1.0f },
};

static const Mel_Vec3 s_cube_normals[] = {
    { .x =  0.0f, .y =  0.0f, .z =  1.0f },
    { .x =  0.0f, .y =  0.0f, .z =  1.0f },
    { .x =  0.0f, .y =  0.0f, .z =  1.0f },
    { .x =  0.0f, .y =  0.0f, .z =  1.0f },

    { .x =  0.0f, .y =  0.0f, .z = -1.0f },
    { .x =  0.0f, .y =  0.0f, .z = -1.0f },
    { .x =  0.0f, .y =  0.0f, .z = -1.0f },
    { .x =  0.0f, .y =  0.0f, .z = -1.0f },

    { .x = -1.0f, .y =  0.0f, .z =  0.0f },
    { .x = -1.0f, .y =  0.0f, .z =  0.0f },
    { .x = -1.0f, .y =  0.0f, .z =  0.0f },
    { .x = -1.0f, .y =  0.0f, .z =  0.0f },

    { .x =  1.0f, .y =  0.0f, .z =  0.0f },
    { .x =  1.0f, .y =  0.0f, .z =  0.0f },
    { .x =  1.0f, .y =  0.0f, .z =  0.0f },
    { .x =  1.0f, .y =  0.0f, .z =  0.0f },

    { .x =  0.0f, .y =  1.0f, .z =  0.0f },
    { .x =  0.0f, .y =  1.0f, .z =  0.0f },
    { .x =  0.0f, .y =  1.0f, .z =  0.0f },
    { .x =  0.0f, .y =  1.0f, .z =  0.0f },

    { .x =  0.0f, .y = -1.0f, .z =  0.0f },
    { .x =  0.0f, .y = -1.0f, .z =  0.0f },
    { .x =  0.0f, .y = -1.0f, .z =  0.0f },
    { .x =  0.0f, .y = -1.0f, .z =  0.0f },
};

static const u32 s_cube_indices[] = {
    0, 1, 2, 2, 3, 0,
    4, 5, 6, 6, 7, 4,
    8, 9, 10, 10, 11, 8,
    12, 13, 14, 14, 15, 12,
    16, 17, 18, 18, 19, 16,
    20, 21, 22, 22, 23, 20,
};

static const Mel_Vec3 s_chunk_proxy_positions[] = {
    { .x =  0.0f, .y =  1.0f, .z =  0.0f },
    { .x =  0.0f, .y = -1.0f, .z =  0.0f },
    { .x = -1.0f, .y =  0.0f, .z =  0.0f },
    { .x =  1.0f, .y =  0.0f, .z =  0.0f },
    { .x =  0.0f, .y =  0.0f, .z =  1.0f },
    { .x =  0.0f, .y =  0.0f, .z = -1.0f },
};

static const Mel_Vec3 s_chunk_proxy_normals[] = {
    { .x =  0.0f, .y =  1.0f, .z =  0.0f },
    { .x =  0.0f, .y = -1.0f, .z =  0.0f },
    { .x = -1.0f, .y =  0.0f, .z =  0.0f },
    { .x =  1.0f, .y =  0.0f, .z =  0.0f },
    { .x =  0.0f, .y =  0.0f, .z =  1.0f },
    { .x =  0.0f, .y =  0.0f, .z = -1.0f },
};

static const u32 s_chunk_proxy_indices[] = {
    0, 4, 3,
    0, 3, 5,
    0, 5, 2,
    0, 2, 4,
    1, 3, 4,
    1, 5, 3,
    1, 2, 5,
    1, 4, 2,
};

static const Mel_Mesh s_cube_mesh = {
    .positions = s_cube_positions,
    .normals = s_cube_normals,
    .vertex_count = SDL_arraysize(s_cube_positions),
    .indices = s_cube_indices,
    .index_count = SDL_arraysize(s_cube_indices),
};

static const Mel_Vec4 s_palette[MATERIAL_VARIANT_COUNT] = {
    { .x = 0.92f, .y = 0.40f, .z = 0.30f, .w = 1.0f },
    { .x = 0.24f, .y = 0.66f, .z = 0.96f, .w = 1.0f },
    { .x = 0.28f, .y = 0.86f, .z = 0.54f, .w = 1.0f },
    { .x = 0.98f, .y = 0.78f, .z = 0.26f, .w = 1.0f },
};

static str8 gpu_scene_mode_label(Gpu_Scene_Mode mode)
{
    switch (mode)
    {
        case GPU_SCENE_MODE_LIST: return S8("cpu render list");
        case GPU_SCENE_MODE_DRAW_STREAM: return S8("gpu draw stream");
        case GPU_SCENE_MODE_COMPUTE_INDIRECT: return S8("gpu compute indirect");
        case GPU_SCENE_MODE_AUTO: return S8("auto selection");
        default: return S8("unknown");
    }
}

static str8 gpu_scene_policy_label(Gpu_Scene_Policy policy)
{
    switch (policy)
    {
        case GPU_SCENE_POLICY_DEFAULT: return S8("default");
        case GPU_SCENE_POLICY_PERFORMANCE: return S8("performance");
        case GPU_SCENE_POLICY_PORTABLE: return S8("portable");
        case GPU_SCENE_POLICY_VISIBILITY: return S8("visibility");
        default: return S8("unknown");
    }
}

static const char* gpu_scene_present_mode_label(VkPresentModeKHR mode)
{
    switch (mode)
    {
        case VK_PRESENT_MODE_IMMEDIATE_KHR: return "immediate";
        case VK_PRESENT_MODE_MAILBOX_KHR: return "mailbox";
        case VK_PRESENT_MODE_FIFO_KHR: return "fifo";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "fifo_relaxed";
        default: return "unknown";
    }
}

static void gpu_scene_draw_panel(Mel_Render_List* list, f32 x, f32 y, f32 w, f32 h, Mel_Vec4 color)
{
    mel_draw_sprite(list,
        .pos = mel_vec2(x, y),
        .size = mel_vec2(w, h),
        .color = color,
        .tex = (Mel_Texture_Handle){0},
        .uv = MEL_UV_FULL);
}

static Mel_Mat4 gpu_scene_transform(Mel_Vec3 pos, Mel_Vec3 scale)
{
    return mel_mat4_mul(mel_mat4_translate(pos), mel_mat4_scale(scale));
}

static Mel_Vec3 gpu_scene_chunk_center(i32 chunk_x, i32 chunk_z)
{
    f32 chunk_spacing = 18.0f;
    f32 origin_x = -((CHUNK_GRID_X - 1) * chunk_spacing) * 0.5f;
    f32 origin_z = -((CHUNK_GRID_Z - 1) * chunk_spacing) * 0.5f;
    return mel_vec3(origin_x + chunk_x * chunk_spacing, 0.0f, origin_z + chunk_z * chunk_spacing);
}

static Mel_Vec3 gpu_scene_cube_position(i32 chunk_x, i32 chunk_z, i32 cube_x, i32 cube_z)
{
    Mel_Vec3 chunk_center = gpu_scene_chunk_center(chunk_x, chunk_z);
    f32 cube_spacing = 2.65f;
    f32 local_x = (cube_x - (CHUNK_CUBES_X - 1) * 0.5f) * cube_spacing;
    f32 local_z = (cube_z - (CHUNK_CUBES_Z - 1) * 0.5f) * cube_spacing;
    f32 world_x = chunk_center.x + local_x;
    f32 world_z = chunk_center.z + local_z;
    f32 wave = sinf(world_x * 0.08f) * 0.75f + cosf(world_z * 0.06f) * 0.55f;
    f32 tier = ((cube_x + cube_z + chunk_x + chunk_z) % 5 == 0) ? 1.35f : 0.0f;
    return mel_vec3(world_x, wave + tier, world_z);
}

static u32 gpu_scene_material_index(i32 chunk_x, i32 chunk_z, i32 cube_x, i32 cube_z)
{
    return (u32)((chunk_x * 3 + chunk_z * 5 + cube_x + cube_z) % MATERIAL_VARIANT_COUNT);
}

static void gpu_scene_build_materials(void)
{
    s_surface_template = mel_material_template_create(&(Mel_Material_Template_Desc){
        .name = S8("scene.surface.standard"),
        .family = mel_material_family_find(S8("surface")),
        .profile = S8("surface.standard"),
        .render_domain = 0,
        .fallback_policy = 0,
        .base_color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
    });

    for (u32 i = 0; i < MATERIAL_VARIANT_COUNT; i++)
    {
        s_materials[i] = mel_material_instance_create(s_surface_template,
            .base_color = s_palette[i]);
        mel_material_instance_set_f32(s_materials[i], S8("roughness"), 0.18f + 0.20f * (f32)i);
        mel_material_instance_set_f32(s_materials[i], S8("metallic"), i == 3 ? 0.78f : (i == 1 ? 0.30f : 0.08f));
        mel_material_instance_set_f32(s_materials[i], S8("occlusion"), i == 2 ? 0.78f : 1.0f);
        mel_material_instance_set_vec4(s_materials[i], S8("emissive"),
            i == 3
                ? mel_vec4(0.95f, 0.62f, 0.18f, 0.45f)
                : (i == 1
                    ? mel_vec4(0.10f, 0.22f, 0.46f, 0.35f)
                    : mel_vec4(0.0f, 0.0f, 0.0f, 1.0f)));
    }

    mel_material_table_init(&s_material_table,
        .dev = mel_gpu_dev(),
        .capacity = MATERIAL_VARIANT_COUNT);
    for (u32 i = 0; i < MATERIAL_VARIANT_COUNT; i++)
        mel_material_table_push(&s_material_table, s_materials[i]);
    mel_material_table_upload(&s_material_table);
    s_material_table_source = mel_source_from_material_table(&s_material_table);
}

static void gpu_scene_build_cpu_world(void)
{
    for (i32 chunk_z = 0; chunk_z < CHUNK_GRID_Z; chunk_z++)
    {
        for (i32 chunk_x = 0; chunk_x < CHUNK_GRID_X; chunk_x++)
        {
            for (i32 cube_z = 0; cube_z < CHUNK_CUBES_Z; cube_z++)
            {
                for (i32 cube_x = 0; cube_x < CHUNK_CUBES_X; cube_x++)
                {
                    Mel_Vec3 pos = gpu_scene_cube_position(chunk_x, chunk_z, cube_x, cube_z);
                    Mel_Vec3 scale = mel_vec3(0.78f, 0.78f + 0.10f * ((cube_x + cube_z) & 1), 0.78f);
                    u32 material_index = gpu_scene_material_index(chunk_x, chunk_z, cube_x, cube_z);
                    mel_draw_mesh(&s_world_meshes,
                        .mesh = &s_cube_mesh,
                        .transform = gpu_scene_transform(pos, scale),
                        .color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
                        .material = s_materials[material_index],
                        .depth = (f32)pos.z);
                }
            }
        }
    }
}

static void gpu_scene_build_chunk(Scene_Chunk* chunk, i32 chunk_x, i32 chunk_z)
{
    const u32 vertex_count = CUBES_PER_CHUNK * SDL_arraysize(s_cube_positions);
    const u32 index_count = CUBES_PER_CHUNK * SDL_arraysize(s_cube_indices);
    Scene_Stream_Vertex* vertices = mel_alloc(mel_alloc_heap(), sizeof(Scene_Stream_Vertex) * vertex_count);
    u32* indices = mel_alloc(mel_alloc_heap(), sizeof(u32) * index_count);

    Mel_Vec3 min_p = mel_vec3(FLT_MAX, FLT_MAX, FLT_MAX);
    Mel_Vec3 max_p = mel_vec3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
    u32 vertex_cursor = 0;
    u32 index_cursor = 0;

    for (i32 cube_z = 0; cube_z < CHUNK_CUBES_Z; cube_z++)
    {
        for (i32 cube_x = 0; cube_x < CHUNK_CUBES_X; cube_x++)
        {
            Mel_Vec3 pos = gpu_scene_cube_position(chunk_x, chunk_z, cube_x, cube_z);
            Mel_Vec3 scale = mel_vec3(0.78f, 0.78f + 0.10f * ((cube_x + cube_z) & 1), 0.78f);
            u32 material_index = gpu_scene_material_index(chunk_x, chunk_z, cube_x, cube_z);
            Mel_Mat4 transform = gpu_scene_transform(pos, scale);
            u32 base_vertex = vertex_cursor;

            Mel_Vec3 cube_min = mel_vec3(pos.x - scale.x, pos.y - scale.y, pos.z - scale.z);
            Mel_Vec3 cube_max = mel_vec3(pos.x + scale.x, pos.y + scale.y, pos.z + scale.z);
            min_p.x = SDL_min(min_p.x, cube_min.x);
            min_p.y = SDL_min(min_p.y, cube_min.y);
            min_p.z = SDL_min(min_p.z, cube_min.z);
            max_p.x = SDL_max(max_p.x, cube_max.x);
            max_p.y = SDL_max(max_p.y, cube_max.y);
            max_p.z = SDL_max(max_p.z, cube_max.z);

            for (u32 i = 0; i < SDL_arraysize(s_cube_positions); i++)
            {
                Mel_Vec3 p = mel_mat4_mul_point(transform, s_cube_positions[i]);
                Mel_Vec3 n = mel_mat4_mul_dir(transform, s_cube_normals[i]);
                n = mel_vec3_normalize(n);
                vertices[vertex_cursor++] = (Scene_Stream_Vertex){
                    .x = p.x, .y = p.y, .z = p.z,
                    .nx = n.x, .ny = n.y, .nz = n.z,
                    .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
                    .material_id = material_index,
                };
            }

            for (u32 i = 0; i < SDL_arraysize(s_cube_indices); i++)
                indices[index_cursor++] = base_vertex + s_cube_indices[i];
        }
    }

    chunk->bounds_center = mel_vec3(
        (min_p.x + max_p.x) * 0.5f,
        (min_p.y + max_p.y) * 0.5f,
        (min_p.z + max_p.z) * 0.5f);
    Mel_Vec3 half_extent = mel_vec3(
        (max_p.x - min_p.x) * 0.5f,
        (max_p.y - min_p.y) * 0.5f,
        (max_p.z - min_p.z) * 0.5f);
    chunk->bounds_extent = half_extent;
    chunk->bounds_radius = sqrtf(mel_vec3_len_sq(half_extent));
    chunk->cube_count = CUBES_PER_CHUNK;

    mel_gpu_buffer_init(&chunk->vertex_buffer, mel_gpu_dev(),
        .size = sizeof(Scene_Stream_Vertex) * vertex_count,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU);
    mel_gpu_buffer_init(&chunk->index_buffer, mel_gpu_dev(),
        .size = sizeof(u32) * index_count,
        .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU);

    mel_gpu_buffer_upload(&chunk->vertex_buffer, mel_gpu_dev(), vertices,
        sizeof(Scene_Stream_Vertex) * vertex_count, 0);
    mel_gpu_buffer_upload(&chunk->index_buffer, mel_gpu_dev(), indices,
        sizeof(u32) * index_count, 0);

    chunk->draw_stream = (Mel_Mesh_Gpu_Draw_Stream){
        .vertex_buffer = chunk->vertex_buffer.buffer,
        .index_buffer = chunk->index_buffer.buffer,
        .vertex_count = vertex_count,
        .index_count = index_count,
    };

    str8 draw_name = str8_fmt(mel_alloc_heap(), "scene_gpu_draw_%d_%d", chunk_x, chunk_z);
    chunk->draw_source = mel_source_create(&(Mel_Source_Desc){
        .name = draw_name,
        .kind = MEL_SOURCE_GPU_BUFFER,
        .schema = MEL_SCHEMA_MESH_DRAW_STREAM,
        .access_flags = MEL_SOURCE_ACCESS_CPU_WRITE | MEL_SOURCE_ACCESS_GPU_READ,
        .lifetime = MEL_SOURCE_LIFETIME_EXTERNAL,
        .user = &chunk->draw_stream,
    });
    mel_dealloc(mel_alloc_heap(), draw_name.data);
    mel_source_set_gpu_buffer(chunk->draw_source, &chunk->vertex_buffer);

    mel_dealloc(mel_alloc_heap(), indices);
    mel_dealloc(mel_alloc_heap(), vertices);
}

static void gpu_scene_build_chunks(void)
{
    for (i32 z = 0; z < CHUNK_GRID_Z; z++)
        for (i32 x = 0; x < CHUNK_GRID_X; x++)
            gpu_scene_build_chunk(&s_chunks[z * CHUNK_GRID_X + x], x, z);
}

static void gpu_scene_build_batch(void)
{
    const u32 lod0_vertices_per_chunk = CUBES_PER_CHUNK * SDL_arraysize(s_cube_positions);
    const u32 lod0_indices_per_chunk = CUBES_PER_CHUNK * SDL_arraysize(s_cube_indices);
    const u32 lod1_vertices_per_chunk = SDL_arraysize(s_chunk_proxy_positions);
    const u32 lod1_indices_per_chunk = SDL_arraysize(s_chunk_proxy_indices);
    const u32 total_vertex_count = CHUNK_COUNT * (lod0_vertices_per_chunk + lod1_vertices_per_chunk);
    const u32 total_index_count = CHUNK_COUNT * (lod0_indices_per_chunk + lod1_indices_per_chunk);

    Scene_Stream_Vertex* vertices = mel_alloc(mel_alloc_heap(), sizeof(Scene_Stream_Vertex) * total_vertex_count);
    u32* indices = mel_alloc(mel_alloc_heap(), sizeof(u32) * total_index_count);
    Mel_Mesh_Gpu_Cull_Batch_Record* metadata = mel_alloc(mel_alloc_heap(),
        sizeof(Mel_Mesh_Gpu_Cull_Batch_Record) * CHUNK_COUNT);
    void* commands = mel_calloc(mel_alloc_heap(),
        CHUNK_COUNT * MEL_MESH_INDIRECT_BATCH_COMMAND_STRIDE);

    u32 vertex_cursor = 0;
    u32 index_cursor = 0;
    u32 chunk_index = 0;

    for (i32 chunk_z = 0; chunk_z < CHUNK_GRID_Z; chunk_z++)
    {
        for (i32 chunk_x = 0; chunk_x < CHUNK_GRID_X; chunk_x++, chunk_index++)
        {
            Scene_Chunk* chunk = &s_chunks[chunk_index];
            u32 lod0_first_index = index_cursor;
            i32 lod0_vertex_offset = (i32)vertex_cursor;

            for (i32 cube_z = 0; cube_z < CHUNK_CUBES_Z; cube_z++)
            {
                for (i32 cube_x = 0; cube_x < CHUNK_CUBES_X; cube_x++)
                {
                    Mel_Vec3 pos = gpu_scene_cube_position(chunk_x, chunk_z, cube_x, cube_z);
                    Mel_Vec3 scale = mel_vec3(0.78f, 0.78f + 0.10f * ((cube_x + cube_z) & 1), 0.78f);
                    u32 material_index = gpu_scene_material_index(chunk_x, chunk_z, cube_x, cube_z);
                    Mel_Mat4 transform = gpu_scene_transform(pos, scale);
                    u32 local_base_vertex = vertex_cursor - (u32)lod0_vertex_offset;

                    for (u32 i = 0; i < SDL_arraysize(s_cube_positions); i++)
                    {
                        Mel_Vec3 p = mel_mat4_mul_point(transform, s_cube_positions[i]);
                        Mel_Vec3 n = mel_mat4_mul_dir(transform, s_cube_normals[i]);
                        n = mel_vec3_normalize(n);
                        vertices[vertex_cursor++] = (Scene_Stream_Vertex){
                            .x = p.x, .y = p.y, .z = p.z,
                            .nx = n.x, .ny = n.y, .nz = n.z,
                            .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
                            .material_id = material_index,
                        };
                    }

                    for (u32 i = 0; i < SDL_arraysize(s_cube_indices); i++)
                        indices[index_cursor++] = local_base_vertex + s_cube_indices[i];
                }
            }

            u32 lod1_first_index = index_cursor;
            i32 lod1_vertex_offset = (i32)vertex_cursor;
            Mel_Mat4 proxy_transform = gpu_scene_transform(chunk->bounds_center,
                mel_vec3(chunk->bounds_extent.x, chunk->bounds_extent.y, chunk->bounds_extent.z));
            for (u32 i = 0; i < SDL_arraysize(s_chunk_proxy_positions); i++)
            {
                Mel_Vec3 p = mel_mat4_mul_point(proxy_transform, s_chunk_proxy_positions[i]);
                Mel_Vec3 n = mel_mat4_mul_dir(proxy_transform, s_chunk_proxy_normals[i]);
                n = mel_vec3_normalize(n);
                vertices[vertex_cursor++] = (Scene_Stream_Vertex){
                    .x = p.x, .y = p.y, .z = p.z,
                    .nx = n.x, .ny = n.y, .nz = n.z,
                    .r = 1.0f, .g = 1.0f, .b = 1.0f, .a = 1.0f,
                    .material_id = (u32)(chunk_index % MATERIAL_VARIANT_COUNT),
                };
            }
            for (u32 i = 0; i < SDL_arraysize(s_chunk_proxy_indices); i++)
                indices[index_cursor++] = s_chunk_proxy_indices[i];

            metadata[chunk_index] = (Mel_Mesh_Gpu_Cull_Batch_Record){
                .bounds = mel_vec4(chunk->bounds_center.x, chunk->bounds_center.y,
                    chunk->bounds_center.z, chunk->bounds_radius),
                .lod0_index_count = lod0_indices_per_chunk,
                .lod0_first_index = lod0_first_index,
                .lod0_vertex_offset = lod0_vertex_offset,
                .lod1_index_count = lod1_indices_per_chunk,
                .lod1_first_index = lod1_first_index,
                .lod1_vertex_offset = lod1_vertex_offset,
            };
        }
    }

    mel_gpu_buffer_init(&s_batch.vertex_buffer, mel_gpu_dev(),
        .size = sizeof(Scene_Stream_Vertex) * total_vertex_count,
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU);
    mel_gpu_buffer_init(&s_batch.index_buffer, mel_gpu_dev(),
        .size = sizeof(u32) * total_index_count,
        .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU);
    mel_gpu_buffer_init(&s_batch.metadata_buffer, mel_gpu_dev(),
        .size = sizeof(Mel_Mesh_Gpu_Cull_Batch_Record) * CHUNK_COUNT,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU);
    mel_gpu_buffer_init(&s_batch.indirect_buffer, mel_gpu_dev(),
        .size = MEL_MESH_INDIRECT_BATCH_COMMAND_STRIDE * CHUNK_COUNT,
        .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU);

    mel_gpu_buffer_upload(&s_batch.vertex_buffer, mel_gpu_dev(), vertices,
        sizeof(Scene_Stream_Vertex) * total_vertex_count, 0);
    mel_gpu_buffer_upload(&s_batch.index_buffer, mel_gpu_dev(), indices,
        sizeof(u32) * total_index_count, 0);
    mel_gpu_buffer_upload(&s_batch.metadata_buffer, mel_gpu_dev(), metadata,
        sizeof(Mel_Mesh_Gpu_Cull_Batch_Record) * CHUNK_COUNT, 0);
    mel_gpu_buffer_upload(&s_batch.indirect_buffer, mel_gpu_dev(), commands,
        MEL_MESH_INDIRECT_BATCH_COMMAND_STRIDE * CHUNK_COUNT, 0);

    s_batch.cull_stream = (Mel_Mesh_Gpu_Cull_Batch_Stream){
        .vertex_buffer = s_batch.vertex_buffer.buffer,
        .index_buffer = s_batch.index_buffer.buffer,
        .metadata_buffer = s_batch.metadata_buffer.buffer,
        .indirect_buffer = s_batch.indirect_buffer.buffer,
        .vertex_count = total_vertex_count,
        .index_count = total_index_count,
        .draw_count = CHUNK_COUNT,
        .stride = MEL_MESH_INDIRECT_BATCH_COMMAND_STRIDE,
        .lod_distance = s_lod_distance,
    };

    s_batch.cull_source = mel_source_create(&(Mel_Source_Desc){
        .name = S8("scene_gpu_cull_batch"),
        .kind = MEL_SOURCE_GPU_BUFFER,
        .schema = MEL_SCHEMA_MESH_CULL_BATCH_STREAM,
        .access_flags = MEL_SOURCE_ACCESS_CPU_WRITE | MEL_SOURCE_ACCESS_GPU_READ | MEL_SOURCE_ACCESS_GPU_WRITE,
        .lifetime = MEL_SOURCE_LIFETIME_EXTERNAL,
        .user = &s_batch.cull_stream,
    });
    mel_source_set_gpu_buffer(s_batch.cull_source, &s_batch.indirect_buffer);

    mel_dealloc(mel_alloc_heap(), commands);
    mel_dealloc(mel_alloc_heap(), metadata);
    mel_dealloc(mel_alloc_heap(), indices);
    mel_dealloc(mel_alloc_heap(), vertices);
}

static bool gpu_scene_chunk_visible(const Scene_Chunk* chunk)
{
    Mel_Mat4 vp = mel_camera_vp(&s_world_camera);
    Mel_Vec4 clip = mel_mat4_mul_vec4(vp, mel_vec4(chunk->bounds_center.x, chunk->bounds_center.y, chunk->bounds_center.z, 1.0f));
    f32 radius = fabsf(chunk->bounds_radius);
    f32 x_radius = radius * sqrtf(vp.m[0][0] * vp.m[0][0] + vp.m[0][1] * vp.m[0][1] + vp.m[0][2] * vp.m[0][2]);
    f32 y_radius = radius * sqrtf(vp.m[1][0] * vp.m[1][0] + vp.m[1][1] * vp.m[1][1] + vp.m[1][2] * vp.m[1][2]);
    f32 z_radius = radius * sqrtf(vp.m[2][0] * vp.m[2][0] + vp.m[2][1] * vp.m[2][1] + vp.m[2][2] * vp.m[2][2]);
    f32 w_radius = radius * sqrtf(vp.m[3][0] * vp.m[3][0] + vp.m[3][1] * vp.m[3][1] + vp.m[3][2] * vp.m[3][2]);
    f32 w_min = -clip.w - w_radius;
    f32 w_max = clip.w + w_radius;
    if (clip.z + z_radius < w_min || clip.z - z_radius > w_max)
        return false;
    if (clip.x + x_radius < w_min || clip.x - x_radius > w_max)
        return false;
    if (clip.y + y_radius < w_min || clip.y - y_radius > w_max)
        return false;
    return true;
}

static bool gpu_scene_chunk_uses_lod1(const Scene_Chunk* chunk)
{
    Mel_Vec3 to_camera = mel_vec3_sub(chunk->bounds_center, s_world_camera.position);
    return mel_vec3_len_sq(to_camera) >= s_lod_distance * s_lod_distance;
}

static void gpu_scene_update_camera(void)
{
    f32 angle = s_time * s_orbit_speed;
    Mel_Vec3 eye = mel_vec3(cosf(angle) * s_camera_distance, s_camera_height, sinf(angle) * s_camera_distance);
    s_world_camera.position = eye;
    s_world_camera.view = mel_mat4_look_at(eye, mel_vec3(0.0f, 2.0f, 0.0f), mel_vec3(0.0f, 1.0f, 0.0f));
}

static void gpu_scene_sync_viewport(void)
{
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;
    Mel_View_Handle world_view = mel_render_stage_3d_view(&s_stage, MEL_RENDER_STAGE_3D_LAYER_WORLD);
    i32 w = 0;
    i32 h = 0;
    mel_window_size_pixels(s_window_handle, &w, &h);
    if (w <= 0 || h <= 0)
        return;

    s_overlay_camera.view = MEL_MAT4_IDENTITY;
    s_overlay_camera.projection = mel_mat4_ortho(0.0f, (f32)w, 0.0f, (f32)h, -1.0f, 1.0f);
    mel_view_set_clear_color_enabled(world_view, true);
    mel_view_set_clear_color(world_view, mel_vec4(0.05f, 0.07f, 0.10f, 1.0f));

    if (sc->extent.width != (u32)w || sc->extent.height != (u32)h)
    {
        mel_swapchain_resize(sc, mel_gpu_dev(), (u32)w, (u32)h);
        s_world_camera.projection = mel_mat4_perspective(60.0f * (3.14159265f / 180.0f),
            (f32)w / (f32)h, 0.1f, 250.0f);
    }

    bool ok = mel_render_stage_3d_refresh(&s_stage);
    assert(ok);
}

static Mel_Technique_Policy_Result gpu_scene_mesh_policy(Mel_Frame_Plan_Technique_Ctx* ctx,
    const Mel_Technique_Desc* technique, void* user)
{
    MEL_UNUSED(ctx);
    Gpu_Scene_Policy policy = *(Gpu_Scene_Policy*)user;
    if (policy == GPU_SCENE_POLICY_DEFAULT)
    {
        return (Mel_Technique_Policy_Result){
            .allow = true,
            .priority_bias = 0,
            .kind = MEL_TECHNIQUE_CHECK_OK,
            .reason = S8(""),
        };
    }

    if (policy == GPU_SCENE_POLICY_PERFORMANCE)
    {
        if (str8_ieq(technique->name, S8("mesh.mesh_shader")))
            return (Mel_Technique_Policy_Result){ .allow = true, .priority_bias = 200, .kind = MEL_TECHNIQUE_CHECK_OK, .reason = S8("performance policy prefers mesh shaders") };
        if (str8_ieq(technique->name, S8("mesh.compute_indirect_batch")))
            return (Mel_Technique_Policy_Result){ .allow = true, .priority_bias = 180, .kind = MEL_TECHNIQUE_CHECK_OK, .reason = S8("performance policy prefers batched compute indirect") };
        if (str8_ieq(technique->name, S8("mesh.compute_indirect")))
            return (Mel_Technique_Policy_Result){ .allow = true, .priority_bias = 140, .kind = MEL_TECHNIQUE_CHECK_OK, .reason = S8("performance policy prefers compute-driven indirect") };
        if (str8_ieq(technique->name, S8("mesh.indirect")))
            return (Mel_Technique_Policy_Result){ .allow = true, .priority_bias = 90, .kind = MEL_TECHNIQUE_CHECK_OK, .reason = S8("performance policy prefers indirect gpu submission") };
        if (str8_ieq(technique->name, S8("mesh.draw_stream")))
            return (Mel_Technique_Policy_Result){ .allow = true, .priority_bias = 40, .kind = MEL_TECHNIQUE_CHECK_OK, .reason = S8("performance policy favors gpu draw streams") };
        if (str8_ieq(technique->name, S8("mesh.forward")))
            return (Mel_Technique_Policy_Result){ .allow = true, .priority_bias = -180, .kind = MEL_TECHNIQUE_CHECK_OK, .reason = S8("performance policy leaves forward as fallback") };
    }

    if (policy == GPU_SCENE_POLICY_PORTABLE)
    {
        if (str8_ieq(technique->name, S8("mesh.forward")))
            return (Mel_Technique_Policy_Result){ .allow = true, .priority_bias = 240, .kind = MEL_TECHNIQUE_CHECK_OK, .reason = S8("portable policy prefers forward path") };
        if (str8_ieq(technique->name, S8("mesh.visibility_buffer")))
            return (Mel_Technique_Policy_Result){ .allow = true, .priority_bias = -60, .kind = MEL_TECHNIQUE_CHECK_OK, .reason = S8("portable policy keeps visibility-buffer as a secondary option") };
        if (str8_ieq(technique->name, S8("mesh.draw_stream")))
            return (Mel_Technique_Policy_Result){ .allow = true, .priority_bias = 30, .kind = MEL_TECHNIQUE_CHECK_OK, .reason = S8("portable policy tolerates gpu draw streams") };
        if (str8_ieq(technique->name, S8("mesh.compute_indirect_batch")))
            return (Mel_Technique_Policy_Result){ .allow = true, .priority_bias = -150, .kind = MEL_TECHNIQUE_CHECK_OK, .reason = S8("portable policy de-prioritizes batched compute indirect") };
        if (str8_ieq(technique->name, S8("mesh.compute_indirect")))
            return (Mel_Technique_Policy_Result){ .allow = true, .priority_bias = -120, .kind = MEL_TECHNIQUE_CHECK_OK, .reason = S8("portable policy de-prioritizes compute indirect") };
        if (str8_ieq(technique->name, S8("mesh.mesh_shader")))
            return (Mel_Technique_Policy_Result){ .allow = true, .priority_bias = -260, .kind = MEL_TECHNIQUE_CHECK_OK, .reason = S8("portable policy avoids mesh shaders") };
    }

    if (policy == GPU_SCENE_POLICY_VISIBILITY)
    {
        if (str8_ieq(technique->name, S8("mesh.visibility_buffer")))
            return (Mel_Technique_Policy_Result){ .allow = true, .priority_bias = 420, .kind = MEL_TECHNIQUE_CHECK_OK, .reason = S8("visibility policy prefers visibility-buffer shading") };
        if (str8_ieq(technique->name, S8("mesh.compute_indirect_batch")))
            return (Mel_Technique_Policy_Result){ .allow = true, .priority_bias = 140, .kind = MEL_TECHNIQUE_CHECK_OK, .reason = S8("visibility policy still prefers batched gpu geometry") };
        if (str8_ieq(technique->name, S8("mesh.mesh_shader")))
            return (Mel_Technique_Policy_Result){ .allow = true, .priority_bias = -60, .kind = MEL_TECHNIQUE_CHECK_OK, .reason = S8("visibility policy de-prioritizes mesh shaders for this demo") };
    }

    return (Mel_Technique_Policy_Result){
        .allow = true,
        .priority_bias = 0,
        .kind = MEL_TECHNIQUE_CHECK_OK,
        .reason = S8(""),
    };
}

static void gpu_scene_apply_policy(Gpu_Scene_Policy policy)
{
    mel_render_technique_set_family_policy(&(Mel_Technique_Family_Policy){
        .family = MEL_TECHNIQUE_MESH,
        .fn = gpu_scene_mesh_policy,
        .user = &s_policy,
    });
    s_policy = policy;
}

static void gpu_scene_request_mode(Gpu_Scene_Mode mode)
{
    s_requested_mode = mode;
}

static void gpu_scene_request_policy(Gpu_Scene_Policy policy)
{
    s_requested_policy = policy;
}

static void gpu_scene_detach_world_sources(void)
{
    Mel_View_Handle world_view = mel_render_stage_3d_view(&s_stage, MEL_RENDER_STAGE_3D_LAYER_WORLD);
    mel_view_detach_source(world_view, s_mesh_list_source);
    mel_view_detach_source(world_view, s_material_table_source);
    mel_view_detach_source(world_view, s_batch.cull_source);
    for (u32 i = 0; i < CHUNK_COUNT; i++)
        mel_view_detach_source(world_view, s_chunks[i].draw_source);
}

static void gpu_scene_apply_mode(Gpu_Scene_Mode mode)
{
    gpu_scene_detach_world_sources();

    bool ok = false;
    switch (mode)
    {
        case GPU_SCENE_MODE_LIST:
            ok = mel_render_stage_3d_attach_mesh_source_to_layer(&s_stage,
                MEL_RENDER_STAGE_3D_LAYER_WORLD, s_mesh_list_source);
            break;
        case GPU_SCENE_MODE_DRAW_STREAM:
            ok = mel_render_stage_3d_attach_mesh_source_to_layer(&s_stage,
                MEL_RENDER_STAGE_3D_LAYER_WORLD, s_material_table_source);
            for (u32 i = 0; i < CHUNK_COUNT; i++)
                ok = mel_render_stage_3d_attach_mesh_source_to_layer(&s_stage,
                    MEL_RENDER_STAGE_3D_LAYER_WORLD, s_chunks[i].draw_source) && ok;
            break;
        case GPU_SCENE_MODE_COMPUTE_INDIRECT:
            ok = mel_render_stage_3d_attach_mesh_source_to_layer(&s_stage,
                MEL_RENDER_STAGE_3D_LAYER_WORLD, s_material_table_source);
            ok = mel_render_stage_3d_attach_mesh_source_to_layer(&s_stage,
                MEL_RENDER_STAGE_3D_LAYER_WORLD, s_batch.cull_source) && ok;
            break;
        case GPU_SCENE_MODE_AUTO:
            ok = mel_render_stage_3d_attach_mesh_source_to_layer(&s_stage,
                MEL_RENDER_STAGE_3D_LAYER_WORLD, s_mesh_list_source);
            ok = mel_render_stage_3d_attach_mesh_source_to_layer(&s_stage,
                MEL_RENDER_STAGE_3D_LAYER_WORLD, s_material_table_source) && ok;
            ok = mel_render_stage_3d_attach_mesh_source_to_layer(&s_stage,
                MEL_RENDER_STAGE_3D_LAYER_WORLD, s_batch.cull_source) && ok;
            for (u32 i = 0; i < CHUNK_COUNT; i++)
                ok = mel_render_stage_3d_attach_mesh_source_to_layer(&s_stage,
                    MEL_RENDER_STAGE_3D_LAYER_WORLD, s_chunks[i].draw_source) && ok;
            break;
        default:
            break;
    }

    assert(ok);
    ok = mel_render_stage_3d_rebuild(&s_stage);
    assert(ok);
    s_mode = mode;
}

static u32 gpu_scene_collect_technique_diag_lines(char lines[][192], u32 max_lines)
{
    Mel_Frame_Plan_Handle plan = mel_render_stage_3d_plan(&s_stage);
    Mel_View_Handle world_view = mel_render_stage_3d_view(&s_stage, MEL_RENDER_STAGE_3D_LAYER_WORLD);
    u32 out = 0;
    u32 count = mel_frame_plan_technique_diagnostic_count(plan);
    for (u32 i = 0; i < count && out < max_lines; i++)
    {
        Mel_Frame_Plan_Technique_Diagnostic diag = {0};
        if (!mel_frame_plan_technique_diagnostic_at(plan, i, &diag))
            continue;
        if (diag.view.handle.index != world_view.handle.index ||
            diag.view.handle.generation != world_view.handle.generation)
            continue;

        SDL_snprintf(lines[out], 192, "%s %s: %.*s",
            diag.selected ? "[*]" : (diag.matched ? "[+]" : "[-]"),
            diag.supported ? "ok" : "no",
            (int)diag.reason.len, diag.reason.data);
        out++;
    }
    return out;
}

static u32 gpu_scene_collect_material_diag_lines(char lines[][192], u32 max_lines)
{
    Mel_Frame_Plan_Handle plan = mel_render_stage_3d_plan(&s_stage);
    Mel_View_Handle world_view = mel_render_stage_3d_view(&s_stage, MEL_RENDER_STAGE_3D_LAYER_WORLD);
    u32 out = 0;
    u32 count = mel_frame_plan_material_diagnostic_count(plan);
    for (u32 i = 0; i < count && out < max_lines; i++)
    {
        Mel_Frame_Plan_Material_Diagnostic diag = {0};
        if (!mel_frame_plan_material_diagnostic_at(plan, i, &diag))
            continue;
        if (diag.view.handle.index != world_view.handle.index ||
            diag.view.handle.generation != world_view.handle.generation)
            continue;

        SDL_snprintf(lines[out], 192, "%s %.*s -> %.*s",
            diag.selected ? "[*]" : (diag.matched ? "[+]" : "[-]"),
            (int)diag.technique_name.len, diag.technique_name.data,
            (int)diag.backend_name.len, diag.backend_name.data);
        out++;
    }
    return out;
}

static void gpu_scene_extract(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(dt);
    MEL_UNUSED(user);

    mel_render_list_clear(&s_hud_sprites);
    mel_render_list_clear(&s_hud_text);

    i32 viewport_w = 0;
    i32 viewport_h = 0;
    mel_window_size_pixels(s_window_handle, &viewport_w, &viewport_h);
    f32 hud_w = (f32)viewport_w;
    Mel_Vec4 panel = mel_vec4(0.07f, 0.09f, 0.13f, 0.86f);
    Mel_Vec4 subpanel = mel_vec4(0.10f, 0.13f, 0.18f, 0.78f);
    Mel_Text_Style title = mel_text_style(mel_vec4(0.95f, 0.97f, 0.99f, 1.0f));
    Mel_Text_Style body = mel_text_style(mel_vec4(0.82f, 0.88f, 0.94f, 1.0f));

    char line[256];
    char diag_lines[10][192];
    char material_lines[6][192];
    u32 diag_count = gpu_scene_collect_technique_diag_lines(diag_lines, SDL_arraysize(diag_lines));
    u32 material_count = gpu_scene_collect_material_diag_lines(material_lines, SDL_arraysize(material_lines));

    Mel_Frame_Plan_Resolved_Technique resolved = {0};
    Mel_Frame_Plan_Resolved_Material resolved_material = {0};
    bool has_resolved = mel_frame_plan_resolved_technique_at(mel_render_stage_3d_plan(&s_stage), 0, &resolved);
    bool has_material = mel_frame_plan_resolved_material_at(mel_render_stage_3d_plan(&s_stage), 0, &resolved_material);
    Mel_Frame_Stats frame = mel_frame_stats();
    Mel_Sim_Stats sim_stats = mel_sim_stats(&s_sim);
    Mel_Gpu_Capabilities caps = mel_gpu_capabilities(mel_gpu_dev());
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;
    Mel_Render_Graph* graph = mel_render_stage_3d_graph(&s_stage);

    gpu_scene_draw_panel(&s_hud_sprites, 28.0f, 28.0f, 470.0f, 228.0f, panel);
    gpu_scene_draw_panel(&s_hud_sprites, hud_w - 340.0f, 28.0f, 312.0f, 188.0f, subpanel);

    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, S8("GPU Scene Playground"),
        .x = 48.0f, .y = 42.0f, .style = title);
    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text,
        S8("Chunk-clustered scene using the same stage/view/recipe model across cpu, draw-stream,\nclustered compute-indirect LOD, and auto-selected modern rendering paths."),
        .x = 48.0f, .y = 76.0f, .style = body);

    SDL_snprintf(line, sizeof(line), "mode %.*s  policy %.*s",
        (int)gpu_scene_mode_label(s_mode).len, gpu_scene_mode_label(s_mode).data,
        (int)gpu_scene_policy_label(s_policy).len, gpu_scene_policy_label(s_policy).data);
    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, str8_from_cstr(line),
        .x = 48.0f, .y = 128.0f, .style = body);

    SDL_snprintf(line, sizeof(line), "clusters %u total  expected visible %u  lod1 %u  cubes %u",
        CHUNK_COUNT, s_expected_visible_chunks, s_expected_lod1_chunks, TOTAL_CUBES);
    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, str8_from_cstr(line),
        .x = 48.0f, .y = 156.0f, .style = body);

    SDL_snprintf(line, sizeof(line), "frame %.2f ms  fps %.1f  sim dt %.2f ms  steps %u",
        frame.dt * 1000.0f, frame.fps, sim_stats.last_scaled_dt * 1000.0f, sim_stats.fixed_steps);
    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, str8_from_cstr(line),
        .x = 48.0f, .y = 184.0f, .style = body);

    SDL_snprintf(line, sizeof(line), "caps mesh_shader=%s bda=%s portability=%s",
        caps.mesh_shader ? "yes" : "no",
        caps.buffer_device_address ? "yes" : "no",
        caps.portability_subset ? "yes" : "no");
    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, str8_from_cstr(line),
        .x = 48.0f, .y = 212.0f, .style = body);

    SDL_snprintf(line, sizeof(line), "present %s  passes %zu  exec %" PRIu64,
        gpu_scene_present_mode_label(mel_swapchain_present_mode(sc)),
        graph ? graph->passes.count : 0,
        graph ? graph->execute_count : 0);
    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, str8_from_cstr(line),
        .x = 48.0f, .y = 240.0f, .style = body);

    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, S8("Resolved"),
        .x = hud_w - 320.0f, .y = 42.0f, .style = title);
    if (has_resolved)
    {
        SDL_snprintf(line, sizeof(line), "technique: %.*s",
            (int)resolved.technique_name.len, resolved.technique_name.data);
        mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, str8_from_cstr(line),
            .x = hud_w - 320.0f, .y = 78.0f, .style = body);
    }
    if (has_material)
    {
        SDL_snprintf(line, sizeof(line), "material: %.*s",
            (int)resolved_material.backend_name.len, resolved_material.backend_name.data);
        mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, str8_from_cstr(line),
            .x = hud_w - 320.0f, .y = 106.0f, .style = body);
    }
    for (u32 i = 0; i < diag_count && i < 3; i++)
    {
        mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, str8_from_cstr(diag_lines[i]),
            .x = hud_w - 320.0f, .y = 136.0f + i * 24.0f, .style = body);
    }
    for (u32 i = 0; i < material_count && i < 2; i++)
    {
        mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, str8_from_cstr(material_lines[i]),
            .x = hud_w - 320.0f, .y = 136.0f + (diag_count < 3 ? diag_count : 3) * 24.0f + i * 24.0f,
            .style = body);
    }
}

static void gpu_scene_tools_imgui(void* user)
{
    MEL_UNUSED(user);

    i32 mode = s_mode;
    i32 policy = s_policy;

    igSetNextWindowPos((ImVec2){ 24.0f, 280.0f }, ImGuiCond_FirstUseEver, (ImVec2){0});
    igSetNextWindowSize((ImVec2){ 420.0f, 380.0f }, ImGuiCond_FirstUseEver);
    if (igBegin("GPU Scene Controls", nullptr, 0))
    {
        igText("One chunked scene, multiple renderer variants.");
        igSeparator();
        if (igRadioButton_IntPtr("Mode: CPU render list", &mode, GPU_SCENE_MODE_LIST))
            gpu_scene_request_mode(GPU_SCENE_MODE_LIST);
        if (igRadioButton_IntPtr("Mode: GPU draw stream", &mode, GPU_SCENE_MODE_DRAW_STREAM))
            gpu_scene_request_mode(GPU_SCENE_MODE_DRAW_STREAM);
        if (igRadioButton_IntPtr("Mode: GPU compute indirect", &mode, GPU_SCENE_MODE_COMPUTE_INDIRECT))
            gpu_scene_request_mode(GPU_SCENE_MODE_COMPUTE_INDIRECT);
        if (igRadioButton_IntPtr("Mode: Auto selection", &mode, GPU_SCENE_MODE_AUTO))
            gpu_scene_request_mode(GPU_SCENE_MODE_AUTO);

        igSeparator();
        if (igRadioButton_IntPtr("Policy: Default", &policy, GPU_SCENE_POLICY_DEFAULT))
            gpu_scene_request_policy(GPU_SCENE_POLICY_DEFAULT);
        if (igRadioButton_IntPtr("Policy: Performance", &policy, GPU_SCENE_POLICY_PERFORMANCE))
            gpu_scene_request_policy(GPU_SCENE_POLICY_PERFORMANCE);
        if (igRadioButton_IntPtr("Policy: Portable", &policy, GPU_SCENE_POLICY_PORTABLE))
            gpu_scene_request_policy(GPU_SCENE_POLICY_PORTABLE);
        if (igRadioButton_IntPtr("Policy: Visibility", &policy, GPU_SCENE_POLICY_VISIBILITY))
            gpu_scene_request_policy(GPU_SCENE_POLICY_VISIBILITY);

        igSeparator();
        igCheckbox("Pause Orbit", &s_pause_animation);
        igCheckbox("Show Expected Visibility", &s_show_expected_visibility);
        igSliderFloat("Orbit Speed", &s_orbit_speed, 0.0f, 0.60f, "%.2f", 0);
        igSliderFloat("Camera Distance", &s_camera_distance, 16.0f, 80.0f, "%.2f", 0);
        igSliderFloat("Camera Height", &s_camera_height, 6.0f, 36.0f, "%.2f", 0);

        igSeparator();
        igText("Chunks: %d x %d", CHUNK_GRID_X, CHUNK_GRID_Z);
        igText("Cubes per chunk: %d", CUBES_PER_CHUNK);
        igText("Total cubes: %d", TOTAL_CUBES);
        igSliderFloat("LOD Distance", &s_lod_distance, 12.0f, 96.0f, "%.1f", 0);
        igText("Expected visible clusters: %u", s_expected_visible_chunks);
        igText("Expected LOD1 clusters: %u", s_expected_lod1_chunks);
    }
    igEnd();
}

static void gpu_scene_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(user);

    if (s_requested_policy != s_policy)
        gpu_scene_apply_policy(s_requested_policy);
    if (s_requested_mode != s_mode)
        gpu_scene_apply_mode(s_requested_mode);

    if (!s_pause_animation)
        s_time += dt;
    gpu_scene_update_camera();
    s_batch.cull_stream.lod_distance = s_lod_distance;
    gpu_scene_sync_viewport();

    s_expected_visible_chunks = 0;
    s_expected_lod1_chunks = 0;
    if (s_show_expected_visibility)
    {
        for (u32 i = 0; i < CHUNK_COUNT; i++)
        {
            if (gpu_scene_chunk_visible(&s_chunks[i]))
            {
                s_expected_visible_chunks++;
                if (gpu_scene_chunk_uses_lod1(&s_chunks[i]))
                    s_expected_lod1_chunks++;
            }
        }
    }
    else
    {
        s_expected_visible_chunks = CHUNK_COUNT;
        for (u32 i = 0; i < CHUNK_COUNT; i++)
            if (gpu_scene_chunk_uses_lod1(&s_chunks[i]))
                s_expected_lod1_chunks++;
    }
}

static void gpu_scene_on_init(void)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;

    mel_vfs_mount_native(mel_vfs(), S8("/"), S8("/"), 0, false);

    mel_render_list_init(&s_world_meshes,
        .name = S8("scene3d_gpu_world"),
        .entry_stride = sizeof(Mel_Mesh_Entry),
        .alloc = mel_alloc_heap());
    mel_render_list_init(&s_hud_sprites,
        .name = S8("scene3d_gpu_hud_sprites"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());
    mel_render_list_init(&s_hud_text,
        .name = S8("scene3d_gpu_hud_text"),
        .entry_stride = sizeof(Mel_Text_Entry),
        .alloc = mel_alloc_heap());

    s_world_camera = (Mel_Camera){
        .projection = mel_mat4_perspective(60.0f * (3.14159265f / 180.0f),
            (f32)sc->extent.width / (f32)sc->extent.height, 0.1f, 250.0f),
    };
    s_overlay_camera = (Mel_Camera){
        .view = MEL_MAT4_IDENTITY,
        .projection = mel_mat4_ortho(0.0f, (f32)sc->extent.width, 0.0f, (f32)sc->extent.height, -1.0f, 1.0f),
    };
    gpu_scene_update_camera();

    s_font = mel_font_atlas_pool_load(mel_font_pool(),
        .path = S8("/System/Library/Fonts/Monaco.ttf"),
        .size = 20.0f);

    gpu_scene_build_materials();
    gpu_scene_build_cpu_world();
    gpu_scene_build_chunks();
    gpu_scene_build_batch();
    s_mesh_list_source = mel_source_from_render_list(&s_world_meshes, MEL_SCHEMA_MESH_INSTANCE);

    bool ok = mel_render_stage_3d_init(&s_stage,
        .name = S8("scene3d_gpu"),
        .swapchain = s_swapchain_handle,
        .world_camera = &s_world_camera,
        .hud_camera = &s_overlay_camera,
        .debug_camera = &s_overlay_camera,
        .ui_camera = &s_overlay_camera,
        .clear_color_enabled = true,
        .clear_color = mel_vec4(0.05f, 0.07f, 0.10f, 1.0f),
        .enable_imgui = true,
        .imgui_fn = gpu_scene_tools_imgui,
        .install_as_current_graph = true,
        .dev = dev,
        .mesh_pass = mel_mesh_pass(),
        .sprite_pass = mel_sprite_pass(),
        .text_pass = mel_text_pass(),
        .alloc = mel_alloc_heap());
    assert(ok);

    mel_render_stage_3d_attach_sprite_list_to_layer(&s_stage, MEL_RENDER_STAGE_3D_LAYER_HUD, &s_hud_sprites);
    mel_render_stage_3d_attach_text_list_to_layer(&s_stage, MEL_RENDER_STAGE_3D_LAYER_HUD, &s_hud_text);

    gpu_scene_apply_policy(GPU_SCENE_POLICY_DEFAULT);
    gpu_scene_apply_mode(GPU_SCENE_MODE_AUTO);
    gpu_scene_extract(nullptr, 0.0f, nullptr);

    mel_imgui_init(s_window_handle, &mel_swapchain_registry_get(s_swapchain_handle)->swapchain);
}

void app_init(void)
{
    s_window_handle = mel_window_create(S8("Melody GPU Scene"), .width = WIN_W, .height = WIN_H);
    s_swapchain_handle = mel_gpu_swapchain_create_for_window_ex(mel_gpu_dev(), s_window_handle,
        .preferred_present_mode = VK_PRESENT_MODE_MAILBOX_KHR);

    gpu_scene_on_init();

    mel_sim_init(&s_sim, .event_buffer = s_event_buf, .event_buffer_size = sizeof(s_event_buf));
    mel_sim_add_variable(&s_sim, gpu_scene_update);
    mel_sim_add_variable(&s_sim, gpu_scene_extract);
    mel_register_sim(&s_sim);
}

Mel_App_Config app_config(void)
{
    return (Mel_App_Config){
        .app_name = S8("Melody GPU Scene"),
        .enable_validation = true,
    };
}

void app_shutdown(void)
{
    mel_unregister_sim(&s_sim);
    mel_sim_shutdown(&s_sim);

    mel_render_stage_3d_shutdown(&s_stage);
    mel_render_technique_clear_family_policy(MEL_TECHNIQUE_MESH);
    mel_render_list_shutdown(&s_hud_text);
    mel_render_list_shutdown(&s_hud_sprites);
    mel_render_list_shutdown(&s_world_meshes);

    for (u32 i = 0; i < CHUNK_COUNT; i++)
    {
        mel_source_destroy(s_chunks[i].draw_source);
        mel_gpu_buffer_shutdown(&s_chunks[i].index_buffer, mel_gpu_dev());
        mel_gpu_buffer_shutdown(&s_chunks[i].vertex_buffer, mel_gpu_dev());
    }

    mel_source_destroy(s_batch.cull_source);
    mel_gpu_buffer_shutdown(&s_batch.indirect_buffer, mel_gpu_dev());
    mel_gpu_buffer_shutdown(&s_batch.metadata_buffer, mel_gpu_dev());
    mel_gpu_buffer_shutdown(&s_batch.index_buffer, mel_gpu_dev());
    mel_gpu_buffer_shutdown(&s_batch.vertex_buffer, mel_gpu_dev());
    mel_source_destroy(s_material_table_source);
    mel_source_destroy(s_mesh_list_source);
    mel_material_table_shutdown(&s_material_table);
    for (u32 i = 0; i < MATERIAL_VARIANT_COUNT; i++)
        mel_material_instance_destroy(s_materials[i]);
    mel_material_template_destroy(s_surface_template);
    mel_vfs_unmount(mel_vfs(), S8("/"));
}

void app_event(SDL_Event* event)
{
    mel_process_event(event);

    if (event->type == SDL_EVENT_QUIT)
    {
        mel_quit();
        return;
    }

    if (event->type != SDL_EVENT_KEY_DOWN || event->key.repeat)
        return;

    switch (event->key.scancode)
    {
        case SDL_SCANCODE_ESCAPE: mel_quit(); break;
        case SDL_SCANCODE_1: gpu_scene_request_mode(GPU_SCENE_MODE_LIST); break;
        case SDL_SCANCODE_2: gpu_scene_request_mode(GPU_SCENE_MODE_DRAW_STREAM); break;
        case SDL_SCANCODE_3: gpu_scene_request_mode(GPU_SCENE_MODE_COMPUTE_INDIRECT); break;
        case SDL_SCANCODE_4: gpu_scene_request_mode(GPU_SCENE_MODE_AUTO); break;
        case SDL_SCANCODE_F1: gpu_scene_request_policy(GPU_SCENE_POLICY_DEFAULT); break;
        case SDL_SCANCODE_F2: gpu_scene_request_policy(GPU_SCENE_POLICY_PERFORMANCE); break;
        case SDL_SCANCODE_F3: gpu_scene_request_policy(GPU_SCENE_POLICY_PORTABLE); break;
        case SDL_SCANCODE_F4: gpu_scene_request_policy(GPU_SCENE_POLICY_VISIBILITY); break;
        case SDL_SCANCODE_SPACE: s_pause_animation = !s_pause_animation; break;
        default: break;
    }
}
