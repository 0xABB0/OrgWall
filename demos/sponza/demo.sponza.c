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
#include "allocator.heap.h"
#include "log.h"
#include "sim.ctx.h"
#include "string.str8.h"
#include "vfs.h"
#include "vfs.backend.os.h"

#include "sponza.loader.h"
#include "sponza.scene.h"
#include "sponza.camera.h"

static Mel_Window_Handle s_window;
static Mel_Swapchain_Handle s_swapchain;
static Mel_Render_Target_Handle s_target;
static Mel_Render_Scene* s_scene;
static Mel_Render_Source* s_source;
static Mel_Render_View_Handle s_view;
static Mel_Geometry_Pool s_geo_pool;
static Mel_Render_Handle s_sponza_handle;
static Mel_Sim_Ctx s_sim;
static u8 s_event_buf[4096];
static Sponza_Camera s_camera;
static bool s_sim_initialized;
static bool s_sim_registered;

static void sponza_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(user);
    sponza_camera_update(&s_camera, s_swapchain, s_view, dt);
}

void app_init(void)
{
    const Mel_Alloc* alloc = mel_alloc_heap();
    Mel_Gpu_Device* dev = mel_gpu_dev();

    mel_vfs_mount(S8("/"), mel_vfs_backend_os(), .root = S8("/"));

    Sponza_Scan_Result scan = {0};
    if (!sponza_scan(&scan, alloc))
    {
        mel_log_error("demo.sponza", "failed to scan Sponza asset");
        mel_quit();
        return;
    }

    s_window = mel_window_create(S8("Melody Sponza"), .width = SPONZA_WIN_W, .height = SPONZA_WIN_H);
    s_swapchain = mel_gpu_swapchain_create_for_window(dev, s_window);
    s_target = mel_render_target_from_swapchain(s_swapchain);

    mel_geometry_pool_init(&s_geo_pool,
        .dev = dev, .alloc = alloc,
        .vertex_stride = sponza_vertex_stride(),
        .vertex_capacity = (u64)scan.vertex_count * sponza_vertex_stride(),
        .index_capacity = (u64)scan.index_count * sizeof(u32));
    mel_pipeline_scene_forward_set_geometry_pool(&s_geo_pool);

    Mel_Material_Base_Id forward_lit_id = sponza_ensure_forward_lit_material_base();

    Sponza_Load_Result loaded = {0};
    if (!sponza_load(&loaded, &s_geo_pool, forward_lit_id, alloc))
    {
        mel_log_error("demo.sponza", "failed to load Sponza");
        mel_quit();
        return;
    }
    mel_log_info("demo.sponza", "loaded Sponza: %u primitives, %u materials, %u vertices, %u indices",
        scan.primitive_count, scan.material_count, scan.vertex_count, scan.index_count);

    s_source = mel_source_manual_create(alloc);
    s_scene = mel_render_scene_create(.dev = dev, .alloc = alloc);
    mel_render_scene_attach_source(s_scene, s_source);

    Mel_Render_Camera camera = {
        .view = MEL_MAT4_IDENTITY,
        .projection = MEL_MAT4_IDENTITY,
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

    s_sponza_handle = mel_source_manual_add(s_source,
        loaded.model,
        loaded.bounds,
        (Mel_Render_Info){
            .material_base_id = loaded.binding_count > 0 ? loaded.bindings[0].material_base_id : forward_lit_id,
            .material_idx = loaded.binding_count > 0 ? loaded.bindings[0].material_idx : 0,
            .mesh = loaded.part_count > 0 ? loaded.parts[0].mesh : (Mel_Geometry_Handle){0},
            .layer_mask = 0xFFFFFFFFu,
        });
    mel_source_manual_set_material_bindings(s_source, s_sponza_handle, loaded.bindings, loaded.binding_count);
    mel_source_manual_set_mesh_parts(s_source, s_sponza_handle, loaded.parts, loaded.part_count);

    sponza_scene_apply_lighting(s_scene, loaded.world_center, loaded.world_extents);
    sponza_camera_init(&s_camera, loaded.world_center, loaded.world_extents);
    sponza_load_result_free(&loaded, alloc);

    mel_sim_init(&s_sim, .event_buffer = s_event_buf, .event_buffer_size = sizeof(s_event_buf));
    s_sim_initialized = true;
    mel_sim_add_variable(&s_sim, sponza_update);
    mel_register_sim(&s_sim);
    s_sim_registered = true;

    SDL_SetWindowRelativeMouseMode(mel__window_sdl(s_window), true);
    s_camera.mouse_captured = true;
    mel_log_info("demo.sponza", "controls: mouse look, WASD move, QE/space/ctrl vertical, shift sprint, esc release/quit");
}

void app_shutdown(void)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    mel_gpu_device_wait_idle(dev);

    if (s_sim_registered)
    {
        mel_unregister_sim(&s_sim);
        s_sim_registered = false;
    }
    if (s_sim_initialized)
    {
        mel_sim_shutdown(&s_sim);
        s_sim_initialized = false;
    }
    if (s_camera.mouse_captured)
    {
        if (mel_window_handle_valid(s_window))
            SDL_SetWindowRelativeMouseMode(mel__window_sdl(s_window), false);
        s_camera.mouse_captured = false;
    }
    if (mel_render_view_handle_valid(s_view))
    {
        mel_render_view_destroy(s_view);
        s_view = MEL_RENDER_VIEW_HANDLE_NULL;
    }
    if (s_source != nullptr)
    {
        mel_render_source_destroy(s_source);
        s_source = nullptr;
    }
    if (s_scene != nullptr)
    {
        mel_render_scene_destroy(s_scene);
        s_scene = nullptr;
    }
    if (mel_render_target_handle_valid(s_target))
    {
        mel_render_target_destroy(s_target);
        s_target = MEL_RENDER_TARGET_HANDLE_NULL;
    }
    if (s_geo_pool.dev != nullptr)
    {
        mel_geometry_pool_shutdown(&s_geo_pool);
        s_geo_pool = (Mel_Geometry_Pool){0};
    }

    s_sponza_handle = (Mel_Render_Handle){0};
    s_swapchain = MEL_SWAPCHAIN_HANDLE_NULL;
    s_window = MEL_WINDOW_HANDLE_NULL;

    mel_vfs_unmount(S8("/"));
}

void app_event(SDL_Event* event)
{
    if (event->type == SDL_EVENT_QUIT)
    {
        mel_quit();
        return;
    }

    if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
    {
        s_window = MEL_WINDOW_HANDLE_NULL;
        s_camera.mouse_captured = false;
        return;
    }

    if (!mel_window_handle_valid(s_window))
        return;

    sponza_camera_event(&s_camera, event, mel__window_sdl(s_window));
}
