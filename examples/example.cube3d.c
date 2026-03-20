#include <SDL3/SDL.h>

#include "core.app.h"
#include "core.engine.h"
#include "window.h"
#include "swapchain.h"
#include "gpu.swapchain.h"
#include "gpu.device.h"
#include "gpu.geometry_pool.h"
#include "render.viewport.h"
#include "render.target.h"
#include "render.scene.h"
#include "render.source.manual.h"
#include "render.pipeline.forward3d.h"
#include "render.material_base.h"
#include "render.types.3d.h"
#include "allocator.heap.h"
#include "math.mat4.h"
#include "math.vec3.h"
#include "math.vec4.h"
#include "sim.ctx.h"
#include "string.str8.h"

#define WIN_W 1280
#define WIN_H 720

typedef struct {
    f32 px, py, pz, _pad0;
    f32 nx, ny, nz, _pad1;
    f32 r, g, b, a;
} Cube_Vertex;

typedef struct {
    Mel_Vec4 base_color;
} Unlit_Params;

static Mel_Window_Handle s_window;
static Mel_Swapchain_Handle s_swapchain;
static Mel_Render_Target_Handle s_target;
static Mel_Render_Scene* s_scene;
static Mel_Render_Source* s_source;
static Mel_Render_View_Handle s_view;
static Mel_Geometry_Pool s_geo_pool;
static Mel_Geometry_Handle s_cube_mesh;
static Mel_Render_Handle s_cube_handle;
static Mel_Sim_Ctx s_sim;
static u8 s_event_buf[4096];
static f32 s_time;

static void make_face(Cube_Vertex* out, u32* idx_out, u32 base,
                       f32 p0x, f32 p0y, f32 p0z,
                       f32 p1x, f32 p1y, f32 p1z,
                       f32 p2x, f32 p2y, f32 p2z,
                       f32 p3x, f32 p3y, f32 p3z,
                       f32 nx, f32 ny, f32 nz,
                       f32 cr, f32 cg, f32 cb)
{
    out[0] = (Cube_Vertex){ p0x, p0y, p0z, 0, nx, ny, nz, 0, cr, cg, cb, 1 };
    out[1] = (Cube_Vertex){ p1x, p1y, p1z, 0, nx, ny, nz, 0, cr, cg, cb, 1 };
    out[2] = (Cube_Vertex){ p2x, p2y, p2z, 0, nx, ny, nz, 0, cr, cg, cb, 1 };
    out[3] = (Cube_Vertex){ p3x, p3y, p3z, 0, nx, ny, nz, 0, cr, cg, cb, 1 };

    idx_out[0] = base;     idx_out[1] = base + 1; idx_out[2] = base + 2;
    idx_out[3] = base + 2; idx_out[4] = base + 3; idx_out[5] = base;
}

static void cube3d_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(user);

    s_time += dt;

    Mel_Mat4 rotation = mel_mat4_rotateXYZ(s_time * 0.7f, s_time * 1.1f, s_time * 0.25f);
    Mel_Mat4 scale = mel_mat4_scalef(0.85f);
    Mel_Mat4 model = mel_mat4_mul(rotation, scale);

    mel_source_manual_set_transform(s_source, s_cube_handle, model);

    Mel_Render_Bounds bounds = {
        .center = mel_vec3(0, 0, 0),
        .extents = mel_vec3(1, 1, 1),
    };
    mel_source_manual_set_bounds(s_source, s_cube_handle, bounds);
}

void app_init(void)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Gpu_Device* dev = mel_gpu_dev();

    s_window = mel_window_create(S8("Melody Cube 3D"), .width = WIN_W, .height = WIN_H);
    s_swapchain = mel_gpu_swapchain_create_for_window(dev, s_window);
    s_target = mel_render_target_from_swapchain(s_swapchain);

    mel_geometry_pool_init(&s_geo_pool,
        .dev = dev, .alloc = alloc,
        .vertex_stride = sizeof(Cube_Vertex),
        .vertex_capacity = sizeof(Cube_Vertex) * 64,
        .index_capacity = sizeof(u32) * 128);

    Cube_Vertex verts[24];
    u32 indices[36];

    make_face(&verts[0],  &indices[0],  0,  -1,-1, 1,  1,-1, 1,  1, 1, 1, -1, 1, 1,  0, 0, 1, 0.90f, 0.32f, 0.28f);
    make_face(&verts[4],  &indices[6],  4,   1,-1,-1, -1,-1,-1, -1, 1,-1,  1, 1,-1,  0, 0,-1, 0.23f, 0.52f, 0.92f);
    make_face(&verts[8],  &indices[12], 8,  -1,-1,-1, -1,-1, 1, -1, 1, 1, -1, 1,-1, -1, 0, 0, 0.18f, 0.78f, 0.48f);
    make_face(&verts[12], &indices[18], 12,  1,-1, 1,  1,-1,-1,  1, 1,-1,  1, 1, 1,  1, 0, 0, 0.95f, 0.72f, 0.22f);
    make_face(&verts[16], &indices[24], 16, -1, 1, 1,  1, 1, 1,  1, 1,-1, -1, 1,-1,  0, 1, 0, 0.64f, 0.42f, 0.90f);
    make_face(&verts[20], &indices[30], 20, -1,-1,-1,  1,-1,-1,  1,-1, 1, -1,-1, 1,  0,-1, 0, 0.20f, 0.82f, 0.88f);

    Mel_Geometry_Upload upload = {
        .vertices = verts,
        .vertex_count = 24,
        .indices = indices,
        .index_count = 36,
        .index_type = MEL_GPU_INDEX_TYPE_U32,
    };
    s_cube_mesh = mel_geometry_pool_upload(&s_geo_pool, &upload);

    mel_pipeline_forward3d_set_geometry_pool(&s_geo_pool);

    Mel_Material_Base_Id unlit_id = mel_material_base_find(S8("unlit"));
    if (unlit_id == MEL_MATERIAL_BASE_ID_INVALID)
    {
        unlit_id = mel_material_base_register(&(Mel_Material_Base_Desc){
            .name = S8("unlit"),
            .param_size = sizeof(Unlit_Params),
            .compat = MEL_COMPAT_FORWARD,
        });
    }

    Unlit_Params white = { .base_color = {{ 1, 1, 1, 1 }} };
    Mel_Material_Instance_Id mat_inst = mel_material_base_alloc_instance(unlit_id, &white);

    s_source = mel_source_manual_create(alloc);
    s_scene = mel_render_scene_create(
        .dev = dev,
        .alloc = alloc);
    mel_render_scene_attach_source(s_scene, s_source);

    Mel_Swapchain_Entry* sc_entry = mel_swapchain_registry_get(s_swapchain);
    Mel_Render_Camera camera = {
        .view = mel_mat4_look_at(mel_vec3(0, 0, 5), mel_vec3(0, 0, 0), mel_vec3(0, 1, 0)),
        .projection = mel_mat4_perspective(60.0f * (3.14159265f / 180.0f),
            (f32)sc_entry->swapchain.extent_width / (f32)sc_entry->swapchain.extent_height,
            0.1f, 100.0f),
        .viewport = {{ 0, 0, 1, 1 }},
        .visibility_mask = 0xFFFFFFFF,
        .lod_bias = 1.0f,
        .view_count = 1,
    };

    s_view = mel_render_view_create(
        .scene = s_scene,
        .camera = camera,
        .target = s_target,
        .pipeline = S8("forward_3d"),
        .dev = dev,
        .alloc = alloc);

    s_cube_handle = mel_source_manual_add(s_source,
        MEL_MAT4_IDENTITY,
        (Mel_Render_Bounds){ .center = mel_vec3(0,0,0), .extents = mel_vec3(1,1,1) },
        (Mel_Render_Info){
            .material_base_id = unlit_id,
            .material_idx = mat_inst,
            .mesh = s_cube_mesh,
            .layer_mask = 0xFFFFFFFF,
        });

    mel_sim_init(&s_sim, .event_buffer = s_event_buf, .event_buffer_size = sizeof(s_event_buf));
    mel_sim_add_variable(&s_sim, cube3d_update);
    mel_register_sim(&s_sim);
}

void app_shutdown(void)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    mel_gpu_device_wait_idle(dev);

    mel_unregister_sim(&s_sim);
    mel_sim_shutdown(&s_sim);
    mel_render_view_destroy(s_view);
    mel_render_source_destroy(s_source);
    mel_render_scene_destroy(s_scene);
    mel_render_target_destroy(s_target);
    mel_geometry_pool_shutdown(&s_geo_pool);
}

void app_event(SDL_Event* event)
{
    if (event->type == SDL_EVENT_QUIT)
        mel_quit();
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE)
        mel_quit();
}
