#include "engine.h"
#include "app.h"
#include "allocator.heap.h"
#include "gpu.cmd.h"
#include "gpu.shader.h"
#include "gpu.submit.h"
#include "string.str8.h"

#include <tracy/TracyC.h>
#include <stdlib.h>

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

    engine->tracking = (Mel_Tracking_Allocator*)malloc(sizeof(Mel_Tracking_Allocator));
    mel_tracking_init(engine->tracking, opt.allocator ? opt.allocator : mel_alloc_heap());
    engine->allocator = mel_tracking_allocator(engine->tracking);

    if (!SDL_Vulkan_LoadLibrary("/opt/homebrew/lib/libvulkan.dylib"))
        SDL_Log("Vulkan library load: %s (continuing — window may have loaded it)", SDL_GetError());

    mel_gpu_device_init(&engine->dev,
        .window = opt.window,
        .allocator = &engine->allocator,
        .enable_validation = opt.enable_validation,
        .app_name = opt.app_name);

    i32 w, h;
    SDL_GetWindowSize(opt.window, &w, &h);
    mel_gpu_swapchain_init(&engine->swapchain, &engine->dev, .width = (u32)w, .height = (u32)h);

    mel_slang_init();

    mel_render_frame_init(&engine->frame, .dev = &engine->dev, .swapchain = &engine->swapchain);

    engine->window = opt.window;
    engine->fixed_dt = opt.fixed_dt;
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

    mel_render_frame_shutdown(&engine->frame);
    mel_slang_shutdown();
    mel_gpu_submit_shutdown(&engine->dev);
    mel_gpu_swapchain_shutdown(&engine->swapchain, &engine->dev);
    mel_gpu_device_shutdown(&engine->dev);

    mel_tracking_report(engine->tracking);
    free(engine->tracking);
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
    if (flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN | SDL_WINDOW_OCCLUDED))
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

    if (engine->fixed_dt > 0)
    {
        engine->accumulator += frame_time;
        while (engine->accumulator >= engine->fixed_dt)
        {
            TracyCZoneN(ctx_update, "on_update", true);
            if (app->opt.on_update)
                app->opt.on_update(app, engine->fixed_dt);
            engine->accumulator -= engine->fixed_dt;
            TracyCZoneEnd(ctx_update);
        }
        engine->alpha = engine->accumulator / engine->fixed_dt;
    }
    else
    {
        TracyCZoneN(ctx_update, "on_update", true);
        if (app->opt.on_update)
            app->opt.on_update(app, frame_time);
        TracyCZoneEnd(ctx_update);
        engine->alpha = 1.0f;
    }

    if (engine->resize_requested)
    {
        i32 w, h;
        SDL_GetWindowSize(engine->window, &w, &h);
        if (w > 0 && h > 0)
            mel_gpu_swapchain_recreate(&engine->swapchain, &engine->dev, w, h);
        engine->resize_requested = false;
    }

    TracyCZoneN(ctx_acquire, "frame_begin", true);
    if (!mel_render_frame_begin(&engine->frame))
    {
        i32 w, h;
        SDL_GetWindowSize(engine->window, &w, &h);
        if (w > 0 && h > 0)
            mel_gpu_swapchain_recreate(&engine->swapchain, &engine->dev, w, h);
        TracyCZoneEnd(ctx_acquire);
        TracyCZoneEnd(ctx_iterate);
        TracyCFrameMark;
        return;
    }
    TracyCZoneEnd(ctx_acquire);

    Mel_Gpu_Cmd c = {
        .cmd = mel_render_frame_cmd(&engine->frame),
        .dev = &engine->dev,
    };

    if (engine->imgui_initialized)
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        igNewFrame();
    }

    TracyCZoneN(ctx_render, "on_render", true);
    if (app->opt.on_render)
        app->opt.on_render(app, &c);
    TracyCZoneEnd(ctx_render);

    if (engine->imgui_initialized)
    {
        igRender();
        ImDrawData* draw_data = igGetDrawData();
        if (draw_data && draw_data->CmdListsCount > 0)
        {
            Mel_Gpu_Color_Attachment imgui_att = {
                .image_view = engine->swapchain.image_views[engine->swapchain.current_image],
                .load_op = VK_ATTACHMENT_LOAD_OP_LOAD,
            };

            mel_gpu_cmd_begin_rendering(&c,
                .color_attachments = &imgui_att,
                .color_count = 1,
                .render_width = engine->swapchain.extent.width,
                .render_height = engine->swapchain.extent.height);

            ImGui_ImplVulkan_RenderDrawData(draw_data, c.cmd, VK_NULL_HANDLE);

            mel_gpu_cmd_end_rendering(&c);
        }

        ImGuiIO* io = igGetIO_Nil();
        if (io->ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            igUpdatePlatformWindows();
            igRenderPlatformWindowsDefault(nullptr, nullptr);
        }
    }

    mel_gpu_cmd_image_barrier(&c,
        engine->swapchain.images[engine->swapchain.current_image],
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_ASPECT_COLOR_BIT);

    TracyCZoneN(ctx_present, "frame_end", true);
    mel_render_frame_end(&engine->frame);
    TracyCZoneEnd(ctx_present);

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

void mel_engine_begin_swapchain_pass_opt(Mel_Engine* engine, Mel_Gpu_Cmd* cmd, Mel_Engine_Pass_Opt opt)
{
    assert(engine != nullptr);
    assert(cmd != nullptr);

    mel_gpu_cmd_image_barrier(cmd,
        engine->swapchain.images[engine->swapchain.current_image],
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT);

    mel_gpu_cmd_set_viewport(cmd, 0, 0,
        (f32)engine->swapchain.extent.width, (f32)engine->swapchain.extent.height, 0, 1);
    mel_gpu_cmd_set_scissor(cmd, 0, 0, engine->swapchain.extent.width, engine->swapchain.extent.height);

    Mel_Gpu_Color_Attachment color_att = {
        .image_view = engine->swapchain.image_views[engine->swapchain.current_image],
        .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .clear_r = opt.clear_r,
        .clear_g = opt.clear_g,
        .clear_b = opt.clear_b,
        .clear_a = opt.clear_a,
    };

    mel_gpu_cmd_begin_rendering(cmd,
        .color_attachments = &color_att,
        .color_count = 1,
        .render_width = engine->swapchain.extent.width,
        .render_height = engine->swapchain.extent.height);
}

void mel_engine_end_swapchain_pass(Mel_Engine* engine, Mel_Gpu_Cmd* cmd)
{
    assert(engine != nullptr);
    assert(cmd != nullptr);

    mel_gpu_cmd_end_rendering(cmd);
}
