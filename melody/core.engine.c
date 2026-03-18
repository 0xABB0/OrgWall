#include "core.engine.h"
#include "core.app.h"
#include "sim.ctx.h"
#include "gpu.device.h"
#include "gpu.swapchain.h"
#include "gpu.submit.h"
#include "gpu.cmd.h"
#include "swapchain.h"
#include "render.blit.h"
#include "gpu.image.h"
#include "window.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "gpu.shader.h"
#include "string.str8.h"
#include "boot.registry.h"
#include "async.job.h"
#include "async.signal.h"
#include "event.channel.h"
#include "thread.dispatch.h"
#include "render.viewport.h"
#include "render.view.registry.h"
#include "render.target.h"

#include <stdatomic.h>

#include <tracy/TracyC.h>

#ifndef MEL_MAX_FRAME_TIME
#define MEL_MAX_FRAME_TIME 0.25f
#endif

#ifndef MEL_APP_NAME
#define MEL_APP_NAME "Melody"
#endif

Mel_Event_Channel mel_shutdown_begin;

static Mel_Sim_Ctx* s_sim_head;
static Mel_Frame_Stats s_frame_stats;
static f32 s_fps_accum;
static u32 s_fps_frames;
static u64 s_last_time;
static u64 s_frame_count;
static bool s_initialized;

__attribute__((constructor))
static void mel__engine_register(void)
{
    mel_event_channel_init(&mel_shutdown_begin, mel_alloc_heap());
}

__attribute__((destructor))
static void mel__engine_unregister(void)
{
    mel_event_channel_destroy(&mel_shutdown_begin);
}

extern void app_init(void);
extern void app_shutdown(void);

static _Atomic(bool) s_boot_done;

static void mel__boot_job(void* data)
{
    (void)data;

    Mel_Gpu_Device* dev = mel_gpu_dev();
    str8 app_name = S8(MEL_APP_NAME);

    if (!mel_gpu_device_init(dev,
        .allocator = mel_alloc_heap(),
        .enable_validation = false,
        .app_name = app_name))
    {
        SDL_Log("Failed to initialize GPU device");
        goto done;
    }

    if (!mel_slang_init())
    {
        SDL_Log("Failed to initialize Slang");
        goto done;
    }

    Mel_Counter shader_counter = MEL_COUNTER_INIT;

    Mel_Gpu_Ready_Event gpu_event = {
        .dev = dev,
        .phase_counter = &shader_counter,
    };
    mel_event_channel_fire(&mel_gpu_device_ready, &gpu_event);

    mel_counter_wait(&shader_counter);

    s_last_time = SDL_GetPerformanceCounter();
    s_initialized = true;

    SDL_Log("Melody Engine initialized!");

    app_init();

done:
    atomic_store_explicit(&s_boot_done, true, memory_order_release);
}

void mel_boot(void)
{
    mel_job_init();
    mel__boot_run_wires();
    mel__main_dispatch_init();

    atomic_store(&s_boot_done, false);
    mel_job_run(nullptr, mel__boot_job, nullptr);

    while (!atomic_load_explicit(&s_boot_done, memory_order_acquire))
    {
        mel__main_dispatch_drain();
        SDL_Delay(1);
    }
}

void mel_shutdown(void)
{
    if (!s_initialized) return;

    Mel_Gpu_Device* dev = mel_gpu_dev();
    mel_gpu_device_wait_idle(dev);

    mel_event_channel_fire(&mel_shutdown_begin, nullptr);

    mel_swapchain_registry_destroy_all(dev);
    mel_slang_shutdown();
    mel_gpu_submit_shutdown(dev);
    mel_gpu_device_shutdown(dev);

    s_sim_head = NULL;
    s_initialized = false;

    mel_job_shutdown();
    mel__main_dispatch_shutdown();

    SDL_Log("Melody Engine shutdown complete");
}

void mel_register_sim(Mel_Sim_Ctx* sim)
{
    assert(sim != nullptr);
    assert(sim->next == nullptr);

    sim->next = s_sim_head;
    s_sim_head = sim;
}

void mel_unregister_sim(Mel_Sim_Ctx* sim)
{
    assert(sim != nullptr);

    Mel_Sim_Ctx** pp = &s_sim_head;
    while (*pp)
    {
        if (*pp == sim)
        {
            *pp = sim->next;
            sim->next = NULL;
            return;
        }
        pp = &(*pp)->next;
    }

    assert(false);
}

typedef struct {
    Mel_Render_View** views;
    u32 view_count;
    Mel_Render_Source** synced_sources;
    u32* synced_count;
    u32 synced_cap;
    Mel_Gpu_Device* dev;
} Frame_Render_Batch;

static void frame_render_batch_cb(Mel_Gpu_Cmd* cmd, void* user)
{
    Frame_Render_Batch* batch = (Frame_Render_Batch*)user;

    for (u32 i = 0; i < batch->view_count; i++)
    {
        Mel_Render_View* view = batch->views[i];

        Mel_Render_Source* src = view->source;
        bool already_synced = false;
        for (u32 j = 0; j < *batch->synced_count; j++)
        {
            if (batch->synced_sources[j] == src)
            {
                already_synced = true;
                break;
            }
        }

        if (!already_synced)
        {
            mel_render_view_sync(view);
            assert(*batch->synced_count < batch->synced_cap);
            batch->synced_sources[(*batch->synced_count)++] = src;
        }

        Mel_Render_Target* target = mel_render_target_get(view->target);

        Mel_Render_Draw_Ctx ctx = {
            .cmd           = cmd,
            .target        = target,
            .target_width  = mel_render_target_width(target),
            .target_height = mel_render_target_height(target),
            .target_format = mel_render_target_format(target),
        };

        if (mel_render_view_has_design_resolution(view))
        {
            Mel_Render_Target* design = mel_render_target_get(view->design_target);
            mel_gpu_cmd_transition_image(cmd, &design->offscreen_image, MEL_GPU_IMAGE_LAYOUT_COLOR_ATTACHMENT);
        }

        mel_render_view_draw(view, &ctx);
        if (mel_render_view_has_design_resolution(view))
        {
            Mel_Render_Target* design = mel_render_target_get(view->design_target);
            mel__blit_to_target(cmd, batch->dev,
                design, target,
                mel_render_target_width(target),
                mel_render_target_height(target),
                view->scale_mode);
        }
    }
}

static void mel__engine_render_frame(void)
{
    u32 total_views = mel__view_registry_count();
    if (total_views == 0)
        return;

    Mel_Gpu_Device* dev = mel_gpu_dev();
    if (!dev->ready)
        return;

    Mel_Render_Source* synced_sources[total_views];
    u32 synced_count = 0;

    Mel_Swapchain* visited_swapchains[total_views];
    u32 visited_count = 0;

    for (u32 vi = 0; vi < total_views; vi++)
    {
        Mel_Render_View* view = mel__view_registry_at(vi);
        if (!view->active || !mel_render_target_alive(view->target))
            continue;

        Mel_Render_Target* target = mel_render_target_get(view->target);
        if (target->type != MEL_TARGET_WINDOW)
            continue;

        Mel_Swapchain_Entry* entry = mel_swapchain_registry_get(target->swapchain);
        if (!entry)
            continue;

        Mel_Swapchain* sc = &entry->swapchain;
        bool already_visited = false;
        for (u32 j = 0; j < visited_count; j++)
        {
            if (visited_swapchains[j] == sc)
            {
                already_visited = true;
                break;
            }
        }

        if (!already_visited)
        {
            visited_swapchains[visited_count++] = sc;
        }
    }

    if (visited_count == 0)
        return;

    Mel_Render_View* sc_views[total_views];

    for (u32 si = 0; si < visited_count; si++)
    {
        Mel_Swapchain* sc = visited_swapchains[si];

        for (u32 vi = 0; vi < total_views; vi++)
        {
            Mel_Render_View* view = mel__view_registry_at(vi);
            if (!view->active || !mel_render_target_alive(view->target)) continue;
            Mel_Render_Target* target = mel_render_target_get(view->target);
            if (target->type != MEL_TARGET_WINDOW) continue;
            Mel_Swapchain_Entry* entry = mel_swapchain_registry_get(target->swapchain);
            if (entry && &entry->swapchain == sc && entry->resize_requested)
            {
                i32 pw = 0, ph = 0;
                mel_window_size_pixels(entry->window, &pw, &ph);
                if (pw > 0 && ph > 0)
                    mel_swapchain_resize(sc, dev, (u32)pw, (u32)ph);
                entry->resize_requested = false;
                break;
            }
        }

        if (!mel_swapchain_acquire(sc, dev))
        {
            continue;
        }

        u32 sc_view_count = 0;
        for (u32 vi = 0; vi < total_views; vi++)
        {
            Mel_Render_View* view = mel__view_registry_at(vi);
            if (!view->active || !mel_render_target_alive(view->target))
                continue;

            Mel_Render_Target* target = mel_render_target_get(view->target);
            if (target->type != MEL_TARGET_WINDOW)
                continue;

            Mel_Swapchain_Entry* entry = mel_swapchain_registry_get(target->swapchain);
            if (!entry || &entry->swapchain != sc)
                continue;

            mel_render_target_begin_frame(target);

            sc_views[sc_view_count++] = view;
        }

        for (u32 a = 1; a < sc_view_count; a++)
        {
            Mel_Render_View* key = sc_views[a];
            i32 b = (i32)a - 1;
            while (b >= 0 && sc_views[b]->priority > key->priority)
            {
                sc_views[b + 1] = sc_views[b];
                b--;
            }
            sc_views[b + 1] = key;
        }

        Frame_Render_Batch batch = {
            .views          = sc_views,
            .view_count     = sc_view_count,
            .synced_sources = synced_sources,
            .synced_count   = &synced_count,
            .synced_cap     = total_views,
            .dev            = dev,
        };

        mel_gpu_submit_frame(dev,
            .callback  = frame_render_batch_cb,
            .user      = &batch,
            .swapchain = sc);

        mel_swapchain_present(sc, dev);
    }
}

void mel_frame(void)
{
    mel__main_dispatch_drain();

    if (!s_initialized) return;

    TracyCZoneN(ctx_iterate, "engine_frame", true);

    u64 now = SDL_GetPerformanceCounter();
    u64 freq = SDL_GetPerformanceFrequency();
    f32 frame_time = (f32)(now - s_last_time) / (f32)freq;
    s_last_time = now;

    if (frame_time > MEL_MAX_FRAME_TIME)
        frame_time = MEL_MAX_FRAME_TIME;

    s_frame_stats.dt = frame_time;
    s_fps_accum += frame_time;
    s_fps_frames++;
    if (s_fps_accum >= 0.25f)
    {
        s_frame_stats.fps = s_fps_frames > 0 && s_fps_accum > 0.0f ? (f32)s_fps_frames / s_fps_accum : 0.0f;
        s_fps_accum = 0.0f;
        s_fps_frames = 0;
    }
    else if (s_frame_stats.fps <= 0.0f && frame_time > 0.0f)
    {
        s_frame_stats.fps = 1.0f / frame_time;
    }

    {
        TracyCZoneN(ctx_sims, "tick_simulations", true);
        for (Mel_Sim_Ctx* sim = s_sim_head; sim; sim = sim->next)
            mel_sim_tick(sim, frame_time);
        TracyCZoneEnd(ctx_sims);
    }

    {
        TracyCZoneN(ctx_render, "render_frame", true);
        mel__engine_render_frame();
        TracyCZoneEnd(ctx_render);
    }

    s_frame_count++;
    s_frame_stats.frame_count = s_frame_count;
    TracyCZoneEnd(ctx_iterate);

    TracyCFrameMark;
}

static void mel__handle_window_close(Mel_Window_Handle wh)
{
    Mel_Window_Close_Event close_event = {
        .window = wh,
        .prevent_default = false,
    };

    mel_event_channel_fire(&mel_window_close_requested, &close_event);

    if (close_event.prevent_default)
        return;

    Mel_Gpu_Device* dev = mel_gpu_dev();
    mel_gpu_device_wait_idle(dev);

    Mel_Swapchain_Handle sc = mel_swapchain_registry_find_by_window(wh);
    if (mel_swapchain_handle_valid(sc))
    {
        u32 view_count = mel__view_registry_count();
        for (u32 i = view_count; i > 0; i--)
        {
            Mel_Render_View* view = mel__view_registry_at(i - 1);
            if (!mel_render_target_alive(view->target)) continue;

            Mel_Render_Target* target = mel_render_target_get(view->target);
            if (target->type != MEL_TARGET_WINDOW) continue;
            if (target->swapchain.handle.index != sc.handle.index ||
                target->swapchain.handle.generation != sc.handle.generation) continue;

            Mel_Render_View_Handle vh = mel__view_registry_handle_at(i - 1);
            mel_render_view_destroy(vh);
        }

        mel_render_target_destroy_by_swapchain(sc);
    }

    mel_window_destroy(wh);

    if (mel_window_count() == 0)
        mel_quit();
}

void mel_process_event(SDL_Event* event)
{
    if (event->type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED)
    {
        Mel_Window_Handle wh = mel__window_find_by_id(event->window.windowID);
        if (!mel_window_handle_valid(wh))
            return;

        Mel_Swapchain_Handle sh = mel_swapchain_registry_find_by_window(wh);
        if (!mel_swapchain_handle_valid(sh))
            return;

        Mel_Swapchain_Entry* entry = mel_swapchain_registry_get(sh);
        if (entry)
            entry->resize_requested = true;
    }

    if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
    {
        Mel_Window_Handle wh = mel__window_find_by_id(event->window.windowID);
        if (mel_window_handle_valid(wh))
            mel__handle_window_close(wh);
    }
}

Mel_Frame_Stats mel_frame_stats(void)
{
    return s_frame_stats;
}

