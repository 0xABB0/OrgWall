#include "core.engine.h"
#include "sim.ctx.h"
#include "gpu.device.h"
#include "gpu.swapchain.h"
#include "swapchain.h"
#include "window.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "font.atlas.h"
#include "gpu.shader.h"
#include "gpu.submit.h"
#include "texture.pool.h"
#include "string.str8.h"
#include "boot.registry.h"
#include "async.job.h"
#include "async.signal.h"
#include "event.channel.h"
#include "thread.dispatch.h"

#include <stdatomic.h>

#include <tracy/TracyC.h>

#ifndef MEL_MAX_FRAME_TIME
#define MEL_MAX_FRAME_TIME 0.25f
#endif

#ifndef MEL_APP_NAME
#define MEL_APP_NAME "Melody"
#endif

static Mel_Sim_Ctx* s_sim_head;
static Mel_Frame_Stats s_frame_stats;
static f32 s_fps_accum;
static u32 s_fps_frames;
static u64 s_last_time;
static u64 s_frame_count;
static bool s_initialized;

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
    mel_stage_shutdown_all();

    mel__font_atlas_shutdown();
    mel_texture_pool_shutdown(mel_texture_pool());
    mel_mesh_pass_shutdown(mel_mesh_pass());
    mel_text_pass_shutdown(mel_text_pass());
    mel_sprite_pass_shutdown(mel_sprite_pass());

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
        TracyCZoneN(ctx_stages, "tick_stages", true);
        mel_stage_tick();
        TracyCZoneEnd(ctx_stages);
    }

    s_frame_count++;
    s_frame_stats.frame_count = s_frame_count;
    TracyCZoneEnd(ctx_iterate);

    TracyCFrameMark;
}

void mel_process_event(SDL_Event* event)
{
    (void)event;
}

Mel_Frame_Stats mel_frame_stats(void)
{
    return s_frame_stats;
}

