#include "window.present.2d.h"
#include "ecs.world.h"
#include "render.ecs.2d.h"
#include "gpu.swapchain.h"
#include "swapchain.h"
#include "window.h"
#include "core.engine.h"
#include "collection.slotmap.h"
#include "allocator.heap.h"

typedef struct {
    Mel_Window_Handle window;
    Mel_Swapchain_Handle swapchain;
    Mel_ECS* world;
    Mel_Render_ECS_2D renderer;
} Mel_Window_Present_2D;

static Mel_SlotMap s_presentations;
static bool s_initialized;

static void mel__window_present_2d_sync_current_graph(void)
{
    if (!s_initialized)
        return;

    u32 count = mel_slotmap_count(&s_presentations);
    if (count == 0)
    {
        mel_set_render_graph(nullptr);
        return;
    }

    Mel_Window_Present_2D* items = mel_slotmap_data(&s_presentations);
    Mel_Render_Stage_2D* stage = mel_render_ecs_2d_stage(&items[count - 1].renderer);
    mel_set_render_graph(mel_render_stage_2d_graph(stage));
}

static Mel_Window_Present_2D* mel__window_present_2d_get(Mel_Window_Present_2D_Handle handle)
{
    assert(s_initialized);
    Mel_Window_Present_2D* present = mel_slotmap_get(&s_presentations, handle.handle);
    assert(present != nullptr);
    return present;
}

static void mel__window_present_2d_refresh_window(Mel_Window_Handle window)
{
    assert(mel_window_handle_valid(window));

    i32 w = 0;
    i32 h = 0;
    mel_window_size_pixels(window, &w, &h);
    if (w <= 0 || h <= 0)
        return;

    Mel_Swapchain_Handle resized = MEL_SWAPCHAIN_HANDLE_NULL;
    Mel_Window_Present_2D* items = mel_slotmap_data(&s_presentations);
    u32 count = mel_slotmap_count(&s_presentations);

    for (u32 i = 0; i < count; i++)
    {
        if (items[i].window.handle.index != window.handle.index ||
            items[i].window.handle.generation != window.handle.generation)
            continue;

        if (!mel_swapchain_handle_valid(resized) ||
            resized.handle.index != items[i].swapchain.handle.index ||
            resized.handle.generation != items[i].swapchain.handle.generation)
        {
            Mel_Swapchain* sc = &mel_swapchain_registry_get(items[i].swapchain)->swapchain;
            mel_swapchain_resize(sc, mel_gpu_dev(), (u32)w, (u32)h);
            resized = items[i].swapchain;
        }

        bool ok = mel_render_stage_2d_refresh(mel_render_ecs_2d_stage(&items[i].renderer));
        assert(ok);
    }
}

__attribute__((constructor(502)))
static void mel__window_present_2d_registry_init(void)
{
    mel_slotmap_init(&s_presentations, mel_alloc_heap(),
        .item_size = sizeof(Mel_Window_Present_2D), .initial_capacity = 4);
    s_initialized = true;
}

__attribute__((destructor(502)))
static void mel__window_present_2d_registry_shutdown(void)
{
    if (!s_initialized) return;
    mel_slotmap_free(&s_presentations);
    s_initialized = false;
}

Mel_Window_Present_2D_Handle mel_window_present_world_2d_opt(Mel_Window_Handle window, Mel_Window_Present_2D_Opt opt)
{
    assert(s_initialized);
    assert(mel_window_handle_valid(window));
    assert(opt.world != nullptr);
    assert(opt.world->world != nullptr);
    assert(opt.world_camera != nullptr);

    Mel_Swapchain_Handle swapchain = mel_window_swapchain(window);
    if (!mel_swapchain_handle_valid(swapchain))
        swapchain = mel_gpu_swapchain_create_for_window(mel_gpu_dev(), window);
    assert(mel_swapchain_handle_valid(swapchain));

    Mel_Window_Present_2D present = {
        .window = window,
        .swapchain = swapchain,
        .world = opt.world,
    };

    Mel_SlotMap_Handle raw = mel_slotmap_insert(&s_presentations, &present);
    Mel_Window_Present_2D* stored = mel_slotmap_get(&s_presentations, raw);

    bool ok = mel_render_ecs_2d_init(&stored->renderer,
        .name = opt.name,
        .world = opt.world->world,
        .swapchain = swapchain,
        .world_camera = opt.world_camera,
        .hud_camera = opt.hud_camera,
        .debug_camera = opt.debug_camera,
        .ui_camera = opt.ui_camera,
        .sprite_layer = opt.sprite_layer,
        .text_layer = opt.text_layer,
        .clear_color_enabled = opt.clear_color_enabled,
        .clear_color = opt.clear_color,
        .design_width = opt.design_width,
        .design_height = opt.design_height,
        .enable_imgui = opt.enable_imgui,
        .imgui_fn = opt.imgui_fn,
        .imgui_user = opt.imgui_user,
        .install_as_current_graph = true,
        .font_pool = opt.font_pool,
        .alloc = opt.alloc);
    if (!ok)
    {
        mel_slotmap_remove(&s_presentations, raw);
        return MEL_WINDOW_PRESENT_2D_HANDLE_NULL;
    }

    mel__window_present_2d_sync_current_graph();
    return (Mel_Window_Present_2D_Handle){ .handle = raw };
}

void mel_window_unpresent_2d(Mel_Window_Present_2D_Handle handle)
{
    assert(s_initialized);

    Mel_Window_Present_2D* present = mel__window_present_2d_get(handle);
    mel_render_ecs_2d_shutdown(&present->renderer);
    mel_slotmap_remove(&s_presentations, handle.handle);
    mel__window_present_2d_sync_current_graph();
}

bool mel_window_present_2d_attach_sprite_list(Mel_Window_Present_2D_Handle handle, Mel_Render_List* list)
{
    return mel_window_present_2d_attach_sprite_list_to_layer(handle, MEL_RENDER_STAGE_2D_LAYER_WORLD, list);
}

bool mel_window_present_2d_attach_sprite_list_to_layer(Mel_Window_Present_2D_Handle handle, Mel_Render_Stage_2D_Layer layer, Mel_Render_List* list)
{
    Mel_Window_Present_2D* present = mel__window_present_2d_get(handle);
    if (!mel_render_stage_2d_attach_sprite_list_to_layer(mel_render_ecs_2d_stage(&present->renderer), layer, list))
        return false;
    bool ok = mel_render_stage_2d_rebuild(mel_render_ecs_2d_stage(&present->renderer));
    mel__window_present_2d_sync_current_graph();
    return ok;
}

bool mel_window_present_2d_attach_text_list(Mel_Window_Present_2D_Handle handle, Mel_Render_List* list)
{
    return mel_window_present_2d_attach_text_list_to_layer(handle, MEL_RENDER_STAGE_2D_LAYER_WORLD, list);
}

bool mel_window_present_2d_attach_text_list_to_layer(Mel_Window_Present_2D_Handle handle, Mel_Render_Stage_2D_Layer layer, Mel_Render_List* list)
{
    Mel_Window_Present_2D* present = mel__window_present_2d_get(handle);
    if (!mel_render_stage_2d_attach_text_list_to_layer(mel_render_ecs_2d_stage(&present->renderer), layer, list))
        return false;
    bool ok = mel_render_stage_2d_rebuild(mel_render_ecs_2d_stage(&present->renderer));
    mel__window_present_2d_sync_current_graph();
    return ok;
}

Mel_Render_Stage_2D* mel_window_present_2d_stage(Mel_Window_Present_2D_Handle handle)
{
    Mel_Window_Present_2D* present = mel__window_present_2d_get(handle);
    return mel_render_ecs_2d_stage(&present->renderer);
}

void mel__window_present_2d_tick(void)
{
    if (!s_initialized) return;

    Mel_Window_Present_2D* items = mel_slotmap_data(&s_presentations);
    u32 count = mel_slotmap_count(&s_presentations);

    for (u32 i = 0; i < count; i++)
        mel_render_ecs_2d_extract(&items[i].renderer);
}

void mel__window_present_2d_process_event(const SDL_Event* event)
{
    if (!s_initialized || event == nullptr)
        return;

    switch (event->type)
    {
        case SDL_EVENT_WINDOW_RESIZED:
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        {
            Mel_Window_Handle window = mel__window_find_by_id(event->window.windowID);
            if (!mel_window_handle_valid(window))
                return;
            mel__window_present_2d_refresh_window(window);
        } break;

        default: break;
    }
}

void mel__window_present_2d_shutdown_all(void)
{
    if (!s_initialized) return;

    Mel_Window_Present_2D* items = mel_slotmap_data(&s_presentations);
    u32 count = mel_slotmap_count(&s_presentations);

    for (u32 i = 0; i < count; i++)
        mel_render_ecs_2d_shutdown(&items[i].renderer);

    mel_slotmap_free(&s_presentations);
    mel_slotmap_init(&s_presentations, mel_alloc_heap(),
        .item_size = sizeof(Mel_Window_Present_2D), .initial_capacity = 4);
    mel_set_render_graph(nullptr);
}
