#include "core.app.h"
#include "core.engine.h"
#include "window.h"
#include "swapchain.h"
#include "gpu.swapchain.h"
#include "gpu.device.h"
#include "gpu.texture.h"
#include "render.viewport.h"
#include "render.target.h"
#include "render.scene.h"
#include "render.texture_table.h"
#include "render.source.ecs.2d.h"
#include "render.source.manual.h"
#include "render.pipeline.scene_forward.h"
#include "render.types.3d.h"
#include "render.material_base.h"
#include "gpu.geometry_pool.h"
#include "ecs.2d.transform.h"
#include "ecs.2d.sprite.h"
#include "math.mat4.h"
#include "math.vec2.h"
#include "math.vec3.h"
#include "math.vec4.h"
#include "math.geo.rect.h"
#include "sim.ctx.h"
#include "allocator.heap.h"
#include "string.str8.h"

#include <flecs.h>
#include <math.h>

static Mel_Window_Handle s_win_2d;
static Mel_Window_Handle s_win_3d;
static Mel_Swapchain_Handle s_sc_2d;
static Mel_Swapchain_Handle s_sc_3d;
static Mel_Render_Target_Handle s_target_2d;
static Mel_Render_Target_Handle s_target_3d;

static Mel_Render_Scene* s_scene_2d;
static Mel_Render_Scene* s_scene_3d;
static Mel_Render_Source* s_source_2d;
static Mel_Render_Source* s_source_3d;
static Mel_Render_View_Handle s_view_2d;
static Mel_Render_View_Handle s_view_3d;

static Mel_Geometry_Pool s_geo_pool;
static Mel_Geometry_Handle s_cube_mesh;
static Mel_Render_Handle s_cube_handle;

static ecs_world_t* s_world;
static Mel_Sim_Ctx s_sim;
static u8 s_event_buf[4096];
static f32 s_time;

typedef struct {
    f32 px, py, pz, _p0;
    f32 nx, ny, nz, _p1;
    f32 r, g, b, a;
    f32 u, v, _p2, _p3;
} Cube_Vert;

typedef struct {
    Mel_Vec4 base_color;
    u32 base_color_texture_idx;
    u32 flags;
    f32 alpha_cutoff;
    f32 _pad;
} Unlit_Params;

static void make_face(Cube_Vert* out, u32* idx, u32 base,
                       f32 p0x,f32 p0y,f32 p0z, f32 p1x,f32 p1y,f32 p1z,
                       f32 p2x,f32 p2y,f32 p2z, f32 p3x,f32 p3y,f32 p3z,
                       f32 nx,f32 ny,f32 nz, f32 cr,f32 cg,f32 cb)
{
    out[0] = (Cube_Vert){p0x,p0y,p0z,0, nx,ny,nz,0, cr,cg,cb,1, 0,0,0,0};
    out[1] = (Cube_Vert){p1x,p1y,p1z,0, nx,ny,nz,0, cr,cg,cb,1, 1,0,0,0};
    out[2] = (Cube_Vert){p2x,p2y,p2z,0, nx,ny,nz,0, cr,cg,cb,1, 1,1,0,0};
    out[3] = (Cube_Vert){p3x,p3y,p3z,0, nx,ny,nz,0, cr,cg,cb,1, 0,1,0,0};
    idx[0]=base; idx[1]=base+1; idx[2]=base+2;
    idx[3]=base+2; idx[4]=base+3; idx[5]=base;
}

static void update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim); MEL_UNUSED(user);
    s_time += dt;

    Mel_Mat4 rot = mel_mat4_rotateXYZ(s_time * 0.7f, s_time * 1.1f, s_time * 0.25f);
    Mel_Mat4 model = mel_mat4_mul(rot, mel_mat4_scalef(0.85f));
    mel_source_manual_set_transform(s_source_3d, s_cube_handle, model);
    mel_source_manual_set_bounds(s_source_3d, s_cube_handle,
        (Mel_Render_Bounds){ .center = mel_vec3(0,0,0), .extents = mel_vec3(1,1,1) });
}

void app_init(void)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Gpu_Device* dev = mel_gpu_dev();

    s_win_2d = mel_window_create(S8("2D Sprites"), .width = 640, .height = 480);
    s_sc_2d = mel_gpu_swapchain_create_for_window(dev, s_win_2d);
    s_target_2d = mel_render_target_from_swapchain(s_sc_2d);

    s_win_3d = mel_window_create(S8("3D Cube"), .width = 640, .height = 480);
    s_sc_3d = mel_gpu_swapchain_create_for_window(dev, s_win_3d);
    s_target_3d = mel_render_target_from_swapchain(s_sc_3d);

    s_world = ecs_init();
    mel_component_transform_register(s_world);
    mel_component_sprite_register(s_world);

    ecs_entity_t e0 = ecs_new(s_world);
    ecs_set(s_world, e0, Mel_CTransform, { .pos = mel_vec2(100, 100) });
    ecs_set(s_world, e0, Mel_Sprite, { .size = mel_vec2(80, 80), .color = {{ 1, 0.3f, 0.3f, 1 }}, .uv = mel_rect(0,0,1,1) });

    ecs_entity_t e1 = ecs_new(s_world);
    ecs_set(s_world, e1, Mel_CTransform, { .pos = mel_vec2(300, 200) });
    ecs_set(s_world, e1, Mel_Sprite, { .size = mel_vec2(60, 60), .color = {{ 0.3f, 1, 0.3f, 1 }}, .uv = mel_rect(0,0,1,1) });

    s_source_2d = mel_source_ecs_2d_create(.world = s_world, .alloc = alloc);
    s_scene_2d = mel_render_scene_create(
        .dev = dev,
        .alloc = alloc);
    mel_render_scene_attach_source(s_scene_2d, s_source_2d);

    Mel_Render_Camera cam_2d = {
        .view = MEL_MAT4_IDENTITY,
        .projection = mel_mat4_ortho(0, 640, 480, 0, -1, 1),
        .viewport = {{ 0, 0, 1, 1 }},
        .visibility_mask = 0xFFFFFFFF,
        .lod_bias = 1.0f,
        .view_count = 1,
    };

    s_view_2d = mel_render_view_create(
        .scene = s_scene_2d, .camera = cam_2d, .target = s_target_2d,
        .pipeline = S8("scene_forward"), .dev = dev, .alloc = alloc);

    mel_geometry_pool_init(&s_geo_pool, .dev = dev, .alloc = alloc,
        .vertex_stride = sizeof(Cube_Vert),
        .vertex_capacity = sizeof(Cube_Vert) * 64,
        .index_capacity = sizeof(u32) * 128);

    Cube_Vert verts[24]; u32 indices[36];
    make_face(&verts[0],  &indices[0],  0,  -1,-1, 1,  1,-1, 1,  1, 1, 1, -1, 1, 1,  0, 0, 1, 0.9f,0.3f,0.3f);
    make_face(&verts[4],  &indices[6],  4,   1,-1,-1, -1,-1,-1, -1, 1,-1,  1, 1,-1,  0, 0,-1, 0.2f,0.5f,0.9f);
    make_face(&verts[8],  &indices[12], 8,  -1,-1,-1, -1,-1, 1, -1, 1, 1, -1, 1,-1, -1, 0, 0, 0.2f,0.8f,0.5f);
    make_face(&verts[12], &indices[18], 12,  1,-1, 1,  1,-1,-1,  1, 1,-1,  1, 1, 1,  1, 0, 0, 0.9f,0.7f,0.2f);
    make_face(&verts[16], &indices[24], 16, -1, 1, 1,  1, 1, 1,  1, 1,-1, -1, 1,-1,  0, 1, 0, 0.6f,0.4f,0.9f);
    make_face(&verts[20], &indices[30], 20, -1,-1,-1,  1,-1,-1,  1,-1, 1, -1,-1, 1,  0,-1, 0, 0.2f,0.8f,0.9f);

    s_cube_mesh = mel_geometry_pool_upload(&s_geo_pool,
        &(Mel_Geometry_Upload){ .vertices = verts, .vertex_count = 24,
            .indices = indices, .index_count = 36, .index_type = MEL_GPU_INDEX_TYPE_U32 });

    mel_pipeline_scene_forward_set_geometry_pool(&s_geo_pool);

    Mel_Material_Base_Id unlit = mel_material_base_find(S8("unlit"));
    if (unlit == MEL_MATERIAL_BASE_ID_INVALID)
        unlit = mel_material_base_register(&(Mel_Material_Base_Desc){
            .name = S8("unlit"), .param_size = sizeof(Unlit_Params), .compat = MEL_COMPAT_FORWARD });

    Unlit_Params white = { .base_color = {{ 1,1,1,1 }} };
    Mel_Material_Instance_Id mat = mel_material_base_alloc_instance(unlit, &white);

    s_source_3d = mel_source_manual_create(alloc);
    s_scene_3d = mel_render_scene_create(
        .dev = dev,
        .alloc = alloc);
    mel_render_scene_attach_source(s_scene_3d, s_source_3d);

    Mel_Render_Camera cam_3d = {
        .view = mel_mat4_look_at(mel_vec3(0,0,5), mel_vec3(0,0,0), mel_vec3(0,1,0)),
        .projection = mel_mat4_perspective(60.0f * (3.14159265f / 180.0f), 640.0f / 480.0f, 0.1f, 100.0f),
        .viewport = {{ 0, 0, 1, 1 }},
        .visibility_mask = 0xFFFFFFFF,
        .lod_bias = 1.0f,
        .view_count = 1,
    };

    s_view_3d = mel_render_view_create(
        .scene = s_scene_3d, .camera = cam_3d, .target = s_target_3d,
        .pipeline = S8("scene_forward"), .dev = dev, .alloc = alloc);

    s_cube_handle = mel_source_manual_add(s_source_3d, MEL_MAT4_IDENTITY,
        (Mel_Render_Bounds){ .center = mel_vec3(0,0,0), .extents = mel_vec3(1,1,1) },
        (Mel_Render_Info){ .material_base_id = unlit, .material_idx = mat, .mesh = s_cube_mesh, .layer_mask = 0xFFFFFFFF });

    mel_sim_init(&s_sim, .event_buffer = s_event_buf, .event_buffer_size = sizeof(s_event_buf));
    mel_sim_add_variable(&s_sim, update);
    mel_register_sim(&s_sim);
}

void app_shutdown(void)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    mel_gpu_device_wait_idle(dev);

    mel_unregister_sim(&s_sim);
    mel_sim_shutdown(&s_sim);
    mel_render_view_destroy(s_view_3d);
    mel_render_view_destroy(s_view_2d);
    mel_render_source_destroy(s_source_3d);
    mel_render_source_destroy(s_source_2d);
    mel_render_scene_destroy(s_scene_3d);
    mel_render_scene_destroy(s_scene_2d);
    mel_render_target_destroy(s_target_3d);
    mel_render_target_destroy(s_target_2d);
    mel_geometry_pool_shutdown(&s_geo_pool);
    ecs_fini(s_world);
}

void app_event(SDL_Event* event)
{
    if (event->type == SDL_EVENT_QUIT)
        mel_quit();
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE)
        mel_quit();
}
