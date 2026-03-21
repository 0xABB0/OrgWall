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
#include "render.pipeline.scene_forward.h"
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
} Demo_Vertex;

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
static Mel_Geometry_Handle s_left_mesh;
static Mel_Geometry_Handle s_right_mesh;
static Mel_Render_Handle s_object_handle;
static Mel_Sim_Ctx s_sim;
static u8 s_event_buf[4096];
static f32 s_time;

static void make_face(Demo_Vertex* out, u32* idx_out, u32 base,
                      f32 p0x, f32 p0y, f32 p0z,
                      f32 p1x, f32 p1y, f32 p1z,
                      f32 p2x, f32 p2y, f32 p2z,
                      f32 p3x, f32 p3y, f32 p3z,
                      f32 nx, f32 ny, f32 nz)
{
    out[0] = (Demo_Vertex){ p0x, p0y, p0z, 0, nx, ny, nz, 0, 1, 1, 1, 1 };
    out[1] = (Demo_Vertex){ p1x, p1y, p1z, 0, nx, ny, nz, 0, 1, 1, 1, 1 };
    out[2] = (Demo_Vertex){ p2x, p2y, p2z, 0, nx, ny, nz, 0, 1, 1, 1, 1 };
    out[3] = (Demo_Vertex){ p3x, p3y, p3z, 0, nx, ny, nz, 0, 1, 1, 1, 1 };

    idx_out[0] = base;
    idx_out[1] = base + 1;
    idx_out[2] = base + 2;
    idx_out[3] = base + 2;
    idx_out[4] = base + 3;
    idx_out[5] = base;
}

static Mel_Geometry_Handle make_box_mesh(Mel_Geometry_Pool* pool, f32 cx, f32 sx)
{
    Demo_Vertex verts[24];
    u32 indices[36];
    f32 x0 = cx - sx;
    f32 x1 = cx + sx;
    f32 y0 = -0.8f;
    f32 y1 = 0.8f;
    f32 z0 = -0.35f;
    f32 z1 = 0.35f;

    make_face(&verts[0],  &indices[0],  0,  x0,y0,z1,  x1,y0,z1,  x1,y1,z1,  x0,y1,z1,  0, 0, 1);
    make_face(&verts[4],  &indices[6],  4,  x1,y0,z0,  x0,y0,z0,  x0,y1,z0,  x1,y1,z0,  0, 0,-1);
    make_face(&verts[8],  &indices[12], 8,  x0,y0,z0,  x0,y0,z1,  x0,y1,z1,  x0,y1,z0, -1, 0, 0);
    make_face(&verts[12], &indices[18], 12, x1,y0,z1,  x1,y0,z0,  x1,y1,z0,  x1,y1,z1,  1, 0, 0);
    make_face(&verts[16], &indices[24], 16, x0,y1,z1,  x1,y1,z1,  x1,y1,z0,  x0,y1,z0,  0, 1, 0);
    make_face(&verts[20], &indices[30], 20, x0,y0,z0,  x1,y0,z0,  x1,y0,z1,  x0,y0,z1,  0,-1, 0);

    Mel_Geometry_Upload upload = {
        .vertices = verts,
        .vertex_count = 24,
        .indices = indices,
        .index_count = 36,
        .index_type = MEL_GPU_INDEX_TYPE_U32,
    };
    return mel_geometry_pool_upload(pool, &upload);
}

static void multimaterial_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(user);

    s_time += dt;

    Mel_Mat4 rotation = mel_mat4_rotateXYZ(s_time * 0.5f, s_time * 0.9f, s_time * 0.2f);
    Mel_Mat4 scale = mel_mat4_scalef(1.0f);
    mel_source_manual_set_transform(s_source, s_object_handle, mel_mat4_mul(rotation, scale));
}

void app_init(void)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Gpu_Device* dev = mel_gpu_dev();

    s_window = mel_window_create(S8("Melody Multimaterial Mesh"), .width = WIN_W, .height = WIN_H);
    s_swapchain = mel_gpu_swapchain_create_for_window(dev, s_window);
    s_target = mel_render_target_from_swapchain(s_swapchain);

    mel_geometry_pool_init(&s_geo_pool,
        .dev = dev, .alloc = alloc,
        .vertex_stride = sizeof(Demo_Vertex),
        .vertex_capacity = sizeof(Demo_Vertex) * 64,
        .index_capacity = sizeof(u32) * 128);

    s_left_mesh = make_box_mesh(&s_geo_pool, -0.55f, 0.38f);
    s_right_mesh = make_box_mesh(&s_geo_pool, 0.55f, 0.38f);
    mel_pipeline_scene_forward_set_geometry_pool(&s_geo_pool);

    Mel_Material_Base_Id unlit_id = mel_material_base_find(S8("unlit"));
    if (unlit_id == MEL_MATERIAL_BASE_ID_INVALID)
    {
        unlit_id = mel_material_base_register(&(Mel_Material_Base_Desc){
            .name = S8("unlit"),
            .param_size = sizeof(Unlit_Params),
            .compat = MEL_COMPAT_FORWARD,
        });
    }

    Mel_Material_Instance_Id red_mat = mel_material_base_alloc_instance(unlit_id,
        &(Unlit_Params){ .base_color = {{ 0.95f, 0.24f, 0.18f, 1.0f }} });
    Mel_Material_Instance_Id blue_mat = mel_material_base_alloc_instance(unlit_id,
        &(Unlit_Params){ .base_color = {{ 0.16f, 0.44f, 0.95f, 1.0f }} });

    s_source = mel_source_manual_create(alloc);
    s_scene = mel_render_scene_create(.dev = dev, .alloc = alloc);
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
        .pipeline = S8("scene_forward"),
        .dev = dev,
        .alloc = alloc);

    s_object_handle = mel_source_manual_add(s_source,
        MEL_MAT4_IDENTITY,
        (Mel_Render_Bounds){ .center = mel_vec3(0,0,0), .extents = mel_vec3(1.2f, 1.0f, 0.5f) },
        (Mel_Render_Info){
            .material_base_id = unlit_id,
            .material_idx = red_mat,
            .mesh = s_left_mesh,
            .layer_mask = 0xFFFFFFFF,
        });

    mel_source_manual_set_material_bindings(s_source, s_object_handle, (Mel_Render_Material_Binding[2]){
        { .slot = 0, .material_base_id = unlit_id, .material_idx = red_mat, .flags = 0 },
        { .slot = 1, .material_base_id = unlit_id, .material_idx = blue_mat, .flags = 0 },
    }, 2);

    mel_source_manual_set_mesh_parts(s_source, s_object_handle, (Mel_Render_Mesh_Part[2]){
        { .mesh = s_left_mesh, .material_binding_index = 0, .flags = 0 },
        { .mesh = s_right_mesh, .material_binding_index = 1, .flags = 0 },
    }, 2);

    mel_sim_init(&s_sim, .event_buffer = s_event_buf, .event_buffer_size = sizeof(s_event_buf));
    mel_sim_add_variable(&s_sim, multimaterial_update);
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
