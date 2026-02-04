#include "melody.h"
#include "allocator_tracking.h"
#include <tracy/TracyC.h>
#include <stdlib.h>

bool mel_engine_init_opt(Mel_Engine* engine, Mel_Engine_Opt opt)
{
    assert(engine != nullptr);

    *engine = (Mel_Engine){0};
    engine->opt = opt;
    engine->fixed_dt = MEL_FIXED_DT;

    if (opt.title == nullptr) opt.title = "Melody Engine";
    if (opt.width <= 0) opt.width = 1280;
    if (opt.height <= 0) opt.height = 720;

    engine->tracking = (Mel_Tracking_Allocator*)malloc(sizeof(Mel_Tracking_Allocator));
    mel_tracking_init(engine->tracking, mel_alloc_malloc());
    engine->allocator = mel_tracking_allocator(engine->tracking);

    engine->window = SDL_CreateWindow(opt.title, opt.width, opt.height, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!engine->window)
    {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        return false;
    }

    if (!mel_vk_context_init(&engine->vk,
        .window = engine->window,
        .allocator = &engine->allocator,
        .enable_validation = true,
        .app_name = opt.title))
    {
        SDL_Log("Failed to initialize Vulkan");
        return false;
    }

    if (!mel_vk_swapchain_init(&engine->swapchain, &engine->vk, opt.width, opt.height))
    {
        SDL_Log("Failed to create swapchain");
        return false;
    }

    if (!mel_slang_init())
    {
        SDL_Log("Failed to initialize Slang");
        return false;
    }

    engine->last_time = SDL_GetPerformanceCounter();
    engine->running = true;

    if (engine->opt.on_init)
    {
        engine->opt.on_init(engine);
    }

    return true;
}

void mel_engine_shutdown(Mel_Engine* engine)
{
    assert(engine != nullptr);

    mel_vk_context_wait_idle(&engine->vk);

    if (engine->opt.on_shutdown)
    {
        engine->opt.on_shutdown(engine);
    }

    mel_slang_shutdown();
    mel_vk_swapchain_shutdown(&engine->swapchain, &engine->vk);
    mel_vk_context_shutdown(&engine->vk);

    if (engine->window)
    {
        SDL_DestroyWindow(engine->window);
        engine->window = nullptr;
    }

    mel_tracking_report(engine->tracking);
    free(engine->tracking);
    engine->tracking = nullptr;
}

bool mel_engine_handle_event(Mel_Engine* engine, SDL_Event* event)
{
    assert(engine != nullptr);
    assert(event != nullptr);

    if (event->type == SDL_EVENT_QUIT)
    {
        engine->should_quit = true;
        return true;
    }

    if (event->type == SDL_EVENT_KEY_DOWN)
    {
        if (event->key.scancode == SDL_SCANCODE_ESCAPE)
        {
            engine->should_quit = true;
            return true;
        }
    }

    if (event->type == SDL_EVENT_WINDOW_RESIZED && event->window.windowID == SDL_GetWindowID(engine->window))
    {
        engine->resize_requested = true;
    }

    if (engine->opt.on_event)
    {
        engine->opt.on_event(engine, event);
    }

    return false;
}

void mel_engine_iterate(Mel_Engine* engine)
{
    TracyCZoneN(ctx_iterate, "mel_engine_iterate", true);
    assert(engine != nullptr);

    SDL_WindowFlags flags = SDL_GetWindowFlags(engine->window);
    if (flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN | SDL_WINDOW_OCCLUDED))
    {
        SDL_Delay(16);
        TracyCZoneEnd(ctx_iterate);
        return;
    }

    u64 now = SDL_GetPerformanceCounter();
    u64 freq = SDL_GetPerformanceFrequency();
    f32 frame_time = (f32)(now - engine->last_time) / (f32)freq;
    engine->last_time = now;

    if (frame_time > MEL_MAX_FRAME_TIME)
    {
        frame_time = MEL_MAX_FRAME_TIME;
    }

    engine->frame_dt = frame_time;
    engine->accumulator += frame_time;

    while (engine->accumulator >= engine->fixed_dt)
    {
        TracyCZoneN(ctx_update, "on_update", true);
        if (engine->opt.on_update)
        {
            engine->opt.on_update(engine, engine->fixed_dt);
        }
        engine->accumulator -= engine->fixed_dt;
        TracyCZoneEnd(ctx_update);
    }

    if (engine->resize_requested)
    {
        i32 w, h;
        SDL_GetWindowSize(engine->window, &w, &h);
        if (w > 0 && h > 0)
        {
            mel_vk_swapchain_recreate(&engine->swapchain, &engine->vk, w, h);
        }
        engine->resize_requested = false;
    }

    TracyCZoneN(ctx_acquire, "swapchain_acquire", true);
    if (!mel_vk_swapchain_acquire(&engine->swapchain, &engine->vk))
    {
        i32 w, h;
        SDL_GetWindowSize(engine->window, &w, &h);
        if (w > 0 && h > 0)
        {
            mel_vk_swapchain_recreate(&engine->swapchain, &engine->vk, w, h);
        }
        TracyCZoneEnd(ctx_acquire);
        TracyCZoneEnd(ctx_iterate);
        return;
    }
    TracyCZoneEnd(ctx_acquire);

    f32 alpha = engine->accumulator / engine->fixed_dt;

    TracyCZoneN(ctx_begin_frame, "swapchain_begin_frame", true);
    mel_vk_swapchain_begin_frame(&engine->swapchain, &engine->vk);
    TracyCZoneEnd(ctx_begin_frame);

    TracyCZoneN(ctx_render, "on_render", true);
    if (engine->opt.on_render)
    {
        engine->opt.on_render(engine, alpha);
    }
    TracyCZoneEnd(ctx_render);

    TracyCZoneN(ctx_end_frame, "swapchain_end_frame", true);
    mel_vk_swapchain_end_frame(&engine->swapchain, &engine->vk);
    TracyCZoneEnd(ctx_end_frame);

    TracyCZoneN(ctx_present, "swapchain_present", true);
    if (!mel_vk_swapchain_present(&engine->swapchain, &engine->vk))
    {
        i32 w, h;
        SDL_GetWindowSize(engine->window, &w, &h);
        if (w > 0 && h > 0)
        {
            mel_vk_swapchain_recreate(&engine->swapchain, &engine->vk, w, h);
        }
    }
    TracyCZoneEnd(ctx_present);

    engine->frame_count++;
    TracyCZoneEnd(ctx_iterate);
}

f32 mel_engine_get_fps(Mel_Engine* engine)
{
    assert(engine != nullptr);
    if (engine->frame_dt > 0.0f)
    {
        return 1.0f / engine->frame_dt;
    }
    return 0.0f;
}

const Mel_Alloc* mel_engine_allocator(Mel_Engine* engine)
{
    assert(engine != nullptr);
    return &engine->allocator;
}
