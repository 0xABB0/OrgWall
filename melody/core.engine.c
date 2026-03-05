#include "core.engine.h"
#include "core.app.h"
#include "render.graph.h"
#include "gpu.swapchain.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "allocator.tracking.h"
#include "gpu.shader.h"
#include "gpu.submit.h"
#include "sprite.pass.h"
#include "texture.pool.h"
#include "allocator.h"
#include "string.str8.h"

#include <tracy/TracyC.h>

#define CIMGUI_USE_SDL3
#define CIMGUI_USE_VULKAN
#include <cimgui/cimgui.h>
#include <cimgui/cimgui_impl.h>

static bool engine_imgui_init(Mel_Engine* engine)
{
    VkDescriptorPoolSize pool_sizes[] = {
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 100 },
    };

    VkDescriptorPoolCreateInfo pool_info = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = 100,
        .poolSizeCount = 1,
        .pPoolSizes = pool_sizes,
    };

    if (vkCreateDescriptorPool(engine->dev.device, &pool_info, nullptr, &engine->imgui_pool) != VK_SUCCESS)
    {
        SDL_Log("Failed to create ImGui descriptor pool");
        return false;
    }

    igCreateContext(nullptr);

    ImGuiIO* io = igGetIO_Nil();
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io->ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    igStyleColorsDark(nullptr);

    ImGuiStyle* style = igGetStyle();
    if (io->ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style->WindowRounding = 0.0f;
        style->Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplSDL3_InitForVulkan(engine->window);

    VkPipelineRenderingCreateInfoKHR rendering_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &engine->swapchain.format,
    };

    ImGui_ImplVulkan_InitInfo init_info = {
        .ApiVersion = VK_API_VERSION_1_3,
        .Instance = engine->dev.instance,
        .PhysicalDevice = engine->dev.physical_device,
        .Device = engine->dev.device,
        .QueueFamily = engine->dev.graphics_family,
        .Queue = engine->dev.graphics_queue,
        .DescriptorPool = engine->imgui_pool,
        .MinImageCount = 2,
        .ImageCount = engine->swapchain.image_count,
        .UseDynamicRendering = true,
        .PipelineInfoMain = {
            .PipelineRenderingCreateInfo = rendering_info,
        },
    };

    ImGui_ImplVulkan_Init(&init_info);

    engine->imgui_initialized = true;
    SDL_Log("ImGui initialized");
    return true;
}

bool mel_engine_init_opt(Mel_Engine* engine, Mel_Engine_Opt opt)
{
    assert(engine != nullptr);
    assert(opt.window != nullptr);

    *engine = (Mel_Engine){0};

    const Mel_Alloc* base_alloc = opt.allocator ? opt.allocator : mel_alloc_heap();
    engine->tracking = mel_alloc_type(base_alloc, Mel_Tracking_Allocator);
    mel_tracking_init(engine->tracking, base_alloc);
    engine->allocator = mel_tracking_allocator(engine->tracking);

    if (!mel_gpu_device_init(&engine->dev,
        .window = opt.window,
        .allocator = &engine->allocator,
        .enable_validation = opt.enable_validation,
        .app_name = opt.app_name))
    {
        SDL_Log("Failed to initialize GPU device");
        goto fail_tracking;
    }

    i32 w, h;
    SDL_GetWindowSize(opt.window, &w, &h);
    if (!mel_gpu_swapchain_init(&engine->swapchain, &engine->dev, .width = (u32)w, .height = (u32)h))
    {
        SDL_Log("Failed to initialize swapchain");
        goto fail_device;
    }

    if (!mel_slang_init())
    {
        SDL_Log("Failed to initialize Slang");
        goto fail_swapchain;
    }

    engine->sprite_pass = mel_alloc_type(&engine->allocator, Mel_Sprite_Pass);
    if (!mel_sprite_pass_init(engine->sprite_pass,
        .dev = &engine->dev,
        .color_format = engine->swapchain.format,
        .max_sprites = 4096))
    {
        SDL_Log("Failed to initialize sprite pass");
        goto fail_slang;
    }

    engine->texture_pool = mel_alloc_type(&engine->allocator, Mel_Texture_Pool);
    mel_texture_pool_init(engine->texture_pool, &engine->allocator, &engine->dev,
        .pipeline = &engine->sprite_pass->pipeline);
    engine->sprite_pass->pool = engine->texture_pool;

    engine->window = opt.window;
    engine->max_frame_time = opt.max_frame_time > 0 ? opt.max_frame_time : 0.25f;
    engine->features = MEL_ENGINE_FEATURE_GPU;

    if (opt.enable_imgui)
    {
        if (engine_imgui_init(engine))
            engine->features |= MEL_ENGINE_FEATURE_IMGUI;
    }

    engine->last_time = SDL_GetPerformanceCounter();

    SDL_Log("Melody Engine initialized!");
    return true;

fail_slang:
    mel_slang_shutdown();
fail_swapchain:
    mel_swapchain_shutdown(&engine->swapchain, &engine->dev);
fail_device:
    mel_gpu_device_shutdown(&engine->dev);
fail_tracking:
    {
        const Mel_Alloc* backing = engine->tracking->backing;
        mel_dealloc(backing, engine->tracking);
        engine->tracking = nullptr;
    }
    return false;
}

void mel_engine_shutdown(Mel_Engine* engine)
{
    assert(engine != nullptr);
    if (!engine->window) return;

    vkDeviceWaitIdle(engine->dev.device);

    if (engine->imgui_initialized)
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        igDestroyContext(nullptr);

        if (engine->imgui_pool)
        {
            vkDestroyDescriptorPool(engine->dev.device, engine->imgui_pool, nullptr);
            engine->imgui_pool = VK_NULL_HANDLE;
        }

        engine->imgui_initialized = false;
        SDL_Log("ImGui shutdown");
    }

    mel_texture_pool_shutdown(engine->texture_pool);
    mel_dealloc(&engine->allocator, engine->texture_pool);
    engine->texture_pool = nullptr;

    mel_sprite_pass_shutdown(engine->sprite_pass);
    mel_dealloc(&engine->allocator, engine->sprite_pass);
    engine->sprite_pass = nullptr;

    mel_slang_shutdown();
    mel_gpu_submit_shutdown(&engine->dev);
    mel_swapchain_shutdown(&engine->swapchain, &engine->dev);
    mel_gpu_device_shutdown(&engine->dev);

    mel_tracking_report(engine->tracking);
    const Mel_Alloc* backing = engine->tracking->backing;
    mel_dealloc(backing, engine->tracking);
    engine->tracking = nullptr;

    engine->window = nullptr;
    SDL_Log("Melody Engine shutdown complete");
}

void mel_engine_frame(Mel_Engine* engine, Mel_App* app)
{
    assert(engine != nullptr);
    assert(app != nullptr);

    TracyCZoneN(ctx_iterate, "engine_frame", true);

    SDL_WindowFlags flags = SDL_GetWindowFlags(engine->window);
    SDL_WindowFlags skip_flags = SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN;
    if (!engine->imgui_initialized)
        skip_flags |= SDL_WINDOW_OCCLUDED;
    if (flags & skip_flags)
    {
        SDL_Delay(16);
        TracyCZoneEnd(ctx_iterate);
        TracyCFrameMark;
        return;
    }

    u64 now = SDL_GetPerformanceCounter();
    u64 freq = SDL_GetPerformanceFrequency();
    f32 frame_time = (f32)(now - engine->last_time) / (f32)freq;
    engine->last_time = now;

    if (frame_time > engine->max_frame_time)
        frame_time = engine->max_frame_time;

    engine->frame_dt = frame_time;

    {
        TracyCZoneN(ctx_update, "on_update", true);
        if (app->opt.on_update)
            app->opt.on_update(app, frame_time);
        TracyCZoneEnd(ctx_update);
    }

    if (engine->resize_requested)
    {
        i32 w, h;
        SDL_GetWindowSize(engine->window, &w, &h);
        if (w > 0 && h > 0)
            mel_swapchain_resize(&engine->swapchain, &engine->dev, w, h);
        engine->resize_requested = false;
    }

    if (engine->imgui_initialized)
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        igNewFrame();
    }

    if (engine->render_graph)
    {
        TracyCZoneN(ctx_graph, "render_graph_execute", true);
        if (!mel_render_graph_execute(engine->render_graph))
        {
            i32 w, h;
            SDL_GetWindowSize(engine->window, &w, &h);
            if (w > 0 && h > 0)
                mel_swapchain_resize(&engine->swapchain, &engine->dev, w, h);
        }
        TracyCZoneEnd(ctx_graph);
    }

    if (engine->imgui_initialized)
    {
        ImGuiIO* io = igGetIO_Nil();
        if (io->ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            igUpdatePlatformWindows();
            igRenderPlatformWindowsDefault(nullptr, nullptr);
        }
    }

    engine->frame_count++;
    TracyCZoneEnd(ctx_iterate);

    TracyCFrameMark;

    if (app->should_quit)
        app->should_quit = true;
}

void mel_engine_process_event(Mel_Engine* engine, SDL_Event* event)
{
    assert(engine != nullptr);

    if (engine->imgui_initialized)
        ImGui_ImplSDL3_ProcessEvent(event);

    if (event->type == SDL_EVENT_WINDOW_RESIZED &&
        event->window.windowID == SDL_GetWindowID(engine->window))
    {
        engine->resize_requested = true;
    }
}

