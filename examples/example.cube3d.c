#include <SDL3/SDL.h>

#include "core.app.h"
#include "core.engine.h"
#include "window.h"
#include "swapchain.h"
#include "gpu.swapchain.h"
#include "string.str8.h"
#include "render.stage.3d.h"
#include "render.list.h"
#include "render.camera.h"
#include "mesh.pass.h"
#include "allocator.heap.h"
#include "math.mat4.h"
#include "math.vec4.h"
#include "math.vec3.h"
#include "sim.ctx.h"

#define WIN_W 1280
#define WIN_H 720

static Mel_Window_Handle s_window_handle;
static Mel_Swapchain_Handle s_swapchain_handle;
static Mel_Render_Stage_3D s_stage;
static Mel_Render_List s_mesh_list;
static Mel_Camera s_camera;
static Mel_Sim_Ctx s_sim;
static u8 s_event_buf[4096];
static u32 s_cube_entry;
static f32 s_time;

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

static const Mel_Vec4 s_cube_colors[] = {
    { .x = 0.90f, .y = 0.32f, .z = 0.28f, .w = 1.0f },
    { .x = 0.90f, .y = 0.32f, .z = 0.28f, .w = 1.0f },
    { .x = 0.90f, .y = 0.32f, .z = 0.28f, .w = 1.0f },
    { .x = 0.90f, .y = 0.32f, .z = 0.28f, .w = 1.0f },

    { .x = 0.23f, .y = 0.52f, .z = 0.92f, .w = 1.0f },
    { .x = 0.23f, .y = 0.52f, .z = 0.92f, .w = 1.0f },
    { .x = 0.23f, .y = 0.52f, .z = 0.92f, .w = 1.0f },
    { .x = 0.23f, .y = 0.52f, .z = 0.92f, .w = 1.0f },

    { .x = 0.18f, .y = 0.78f, .z = 0.48f, .w = 1.0f },
    { .x = 0.18f, .y = 0.78f, .z = 0.48f, .w = 1.0f },
    { .x = 0.18f, .y = 0.78f, .z = 0.48f, .w = 1.0f },
    { .x = 0.18f, .y = 0.78f, .z = 0.48f, .w = 1.0f },

    { .x = 0.95f, .y = 0.72f, .z = 0.22f, .w = 1.0f },
    { .x = 0.95f, .y = 0.72f, .z = 0.22f, .w = 1.0f },
    { .x = 0.95f, .y = 0.72f, .z = 0.22f, .w = 1.0f },
    { .x = 0.95f, .y = 0.72f, .z = 0.22f, .w = 1.0f },

    { .x = 0.64f, .y = 0.42f, .z = 0.90f, .w = 1.0f },
    { .x = 0.64f, .y = 0.42f, .z = 0.90f, .w = 1.0f },
    { .x = 0.64f, .y = 0.42f, .z = 0.90f, .w = 1.0f },
    { .x = 0.64f, .y = 0.42f, .z = 0.90f, .w = 1.0f },

    { .x = 0.20f, .y = 0.82f, .z = 0.88f, .w = 1.0f },
    { .x = 0.20f, .y = 0.82f, .z = 0.88f, .w = 1.0f },
    { .x = 0.20f, .y = 0.82f, .z = 0.88f, .w = 1.0f },
    { .x = 0.20f, .y = 0.82f, .z = 0.88f, .w = 1.0f },
};

static const u32 s_cube_indices[] = {
    0, 1, 2, 2, 3, 0,
    4, 5, 6, 6, 7, 4,
    8, 9, 10, 10, 11, 8,
    12, 13, 14, 14, 15, 12,
    16, 17, 18, 18, 19, 16,
    20, 21, 22, 22, 23, 20,
};

static const Mel_Mesh s_cube_mesh = {
    .positions = s_cube_positions,
    .colors = s_cube_colors,
    .vertex_count = SDL_arraysize(s_cube_positions),
    .indices = s_cube_indices,
    .index_count = SDL_arraysize(s_cube_indices),
};

static void cube3d_sync_viewport(void)
{
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;
    i32 w = 0;
    i32 h = 0;
    mel_window_size_pixels(s_window_handle, &w, &h);
    if (w <= 0 || h <= 0)
        return;

    if (sc->extent.width != (u32)w || sc->extent.height != (u32)h)
    {
        mel_swapchain_resize(sc, mel_gpu_dev(), (u32)w, (u32)h);
        s_camera.projection = mel_mat4_perspective(60.0f * (3.14159265f / 180.0f), (f32)w / (f32)h, 0.1f, 100.0f);
        mel_render_stage_3d_refresh(&s_stage);
    }
}

static void cube3d_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(user);

    s_time += dt;
    cube3d_sync_viewport();

    Mel_Mat4 rotation = mel_mat4_rotateXYZ(s_time * 0.7f, s_time * 1.1f, s_time * 0.25f);
    Mel_Mat4 scale = mel_mat4_scalef(0.85f);
    Mel_Mat4 transform = mel_mat4_mul(rotation, scale);

    Mel_Mesh_Entry* cube = mel_render_list_get(&s_mesh_list, s_cube_entry);
    cube->transform = transform;
}

static void cube3d_on_init(void)
{
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;

    mel_render_list_init(&s_mesh_list,
        .name = S8("cube_meshes"),
        .entry_stride = sizeof(Mel_Mesh_Entry),
        .alloc = mel_alloc_heap());

    s_camera = (Mel_Camera){
        .view = mel_mat4_look_at(mel_vec3(0.0f, 0.0f, 5.0f), mel_vec3(0.0f, 0.0f, 0.0f), mel_vec3(0.0f, 1.0f, 0.0f)),
        .projection = mel_mat4_perspective(60.0f * (3.14159265f / 180.0f),
            (f32)sc->extent.width / (f32)sc->extent.height, 0.1f, 100.0f),
        .position = mel_vec3(0.0f, 0.0f, 5.0f),
    };

    mel_render_stage_3d_init(&s_stage,
        .name = S8("cube3d"),
        .swapchain = s_swapchain_handle,
        .world_camera = &s_camera,
        .clear_color_enabled = true,
        .clear_color = mel_vec4(0.06f, 0.07f, 0.10f, 1.0f),
        .install_as_current_graph = true,
        .dev = mel_gpu_dev(),
        .mesh_pass = mel_mesh_pass(),
        .alloc = mel_alloc_heap());

    mel_render_stage_3d_attach_mesh_list(&s_stage, &s_mesh_list);
    mel_render_stage_3d_rebuild(&s_stage);

    s_cube_entry = mel_render_list_insert(&s_mesh_list, mel_sort_key_mesh_opaque(0.0f));
    Mel_Mesh_Entry* cube = mel_render_list_get(&s_mesh_list, s_cube_entry);
    *cube = (Mel_Mesh_Entry){
        .mesh = &s_cube_mesh,
        .transform = MEL_MAT4_IDENTITY,
        .color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
    };
}

void app_init(void)
{
    s_window_handle = mel_window_create(S8("Melody Cube 3D"), .width = WIN_W, .height = WIN_H);
    s_swapchain_handle = mel_gpu_swapchain_create_for_window(mel_gpu_dev(), s_window_handle);

    cube3d_on_init();

    mel_sim_init(&s_sim, .event_buffer = s_event_buf, .event_buffer_size = sizeof(s_event_buf));
    mel_sim_add_variable(&s_sim, cube3d_update);
    mel_register_sim(&s_sim);
}

void app_shutdown(void)
{
    mel_unregister_sim(&s_sim);
    mel_sim_shutdown(&s_sim);
    mel_render_stage_3d_shutdown(&s_stage);
    mel_render_list_shutdown(&s_mesh_list);
}

void app_event(SDL_Event* event)
{
    if (event->type == SDL_EVENT_QUIT)
        mel_quit();
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE)
        mel_quit();
}
