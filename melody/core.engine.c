#include "core.engine.h"
#include "render.graph.h"
#include "sim.ctx.h"
#include "gpu.device.h"
#include "gpu.swapchain.h"
#include "swapchain.h"
#include "window.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "allocator.tracking.h"
#include "gpu.shader.h"
#include "gpu.submit.h"
#include "sprite.pass.h"
#include "texture.pool.h"
#include "string.str8.h"
#include "debug.backtrace.h"

#include <tracy/TracyC.h>

#define CIMGUI_USE_SDL3
#define CIMGUI_USE_VULKAN
#include <cimgui/cimgui.h>
#include <cimgui/cimgui_impl.h>

static Mel_Gpu_Device s_dev;
static Mel_Tracking_Allocator* s_tracking;
static Mel_Alloc s_allocator;
static Mel_Sprite_Pass* s_sprite_pass;
static Mel_Texture_Pool* s_texture_pool;
static Mel_Render_Graph* s_render_graph;
static Mel_Sim_Ctx* s_sim_head;
static f32 s_max_frame_time;
static f32 s_frame_dt;
static u64 s_last_time;
static u64 s_frame_count;
static bool s_initialized;

static VkDescriptorPool s_imgui_pool;
static SDL_Window* s_imgui_window;
static bool s_imgui_initialized;

bool mel_imgui_init(SDL_Window* window, Mel_Swapchain* swapchain)
{
    assert(window != nullptr);
    assert(swapchain != nullptr);

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

    if (vkCreateDescriptorPool(s_dev.device, &pool_info, nullptr, &s_imgui_pool) != VK_SUCCESS)
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

    ImGui_ImplSDL3_InitForVulkan(window);

    VkPipelineRenderingCreateInfoKHR rendering_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &swapchain->format,
    };

    ImGui_ImplVulkan_InitInfo init_info = {
        .ApiVersion = VK_API_VERSION_1_3,
        .Instance = s_dev.instance,
        .PhysicalDevice = s_dev.physical_device,
        .Device = s_dev.device,
        .QueueFamily = s_dev.graphics_family,
        .Queue = s_dev.graphics_queue,
        .DescriptorPool = s_imgui_pool,
        .MinImageCount = 2,
        .ImageCount = swapchain->image_count,
        .UseDynamicRendering = true,
        .PipelineInfoMain = {
            .PipelineRenderingCreateInfo = rendering_info,
        },
    };

    ImGui_ImplVulkan_Init(&init_info);

    s_imgui_window = window;
    s_imgui_initialized = true;
    SDL_Log("ImGui initialized");
    return true;
}

bool mel_init_opt(Mel_Init_Opt opt)
{
    assert(!s_initialized);

    const Mel_Alloc* base_alloc = opt.allocator ? opt.allocator : mel_alloc_heap();
    s_tracking = mel_alloc_type(base_alloc, Mel_Tracking_Allocator);
    mel_tracking_init(s_tracking, base_alloc);
    s_allocator = mel_tracking_allocator(s_tracking);

    if (!mel_gpu_device_init(&s_dev,
        .allocator = &s_allocator,
        .enable_validation = opt.enable_validation,
        .app_name = opt.app_name))
    {
        SDL_Log("Failed to initialize GPU device");
        goto fail_tracking;
    }

    if (!mel_slang_init())
    {
        SDL_Log("Failed to initialize Slang");
        goto fail_device;
    }

    s_sprite_pass = mel_alloc_type(&s_allocator, Mel_Sprite_Pass);
    if (!mel_sprite_pass_init(s_sprite_pass,
        .dev = &s_dev,
        .color_format = VK_FORMAT_B8G8R8A8_SRGB,
        .max_sprites = 4096))
    {
        SDL_Log("Failed to initialize sprite pass");
        goto fail_slang;
    }

    s_texture_pool = mel_alloc_type(&s_allocator, Mel_Texture_Pool);
    mel_texture_pool_init(s_texture_pool, &s_allocator, &s_dev,
        .pipeline = &s_sprite_pass->pipeline);
    s_sprite_pass->pool = s_texture_pool;

    s_max_frame_time = opt.max_frame_time > 0 ? opt.max_frame_time : 0.25f;
    s_last_time = SDL_GetPerformanceCounter();
    s_initialized = true;

    SDL_Log("Melody Engine initialized!");
    return true;

fail_slang:
    mel_slang_shutdown();
fail_device:
    mel_gpu_device_shutdown(&s_dev);
fail_tracking:
    {
        const Mel_Alloc* backing = s_tracking->backing;
        mel_dealloc(backing, s_tracking);
        s_tracking = nullptr;
    }
    return false;
}

void mel_shutdown(void)
{
    if (!s_initialized) return;

    vkDeviceWaitIdle(s_dev.device);

    if (s_imgui_initialized)
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        igDestroyContext(nullptr);

        if (s_imgui_pool)
        {
            vkDestroyDescriptorPool(s_dev.device, s_imgui_pool, nullptr);
            s_imgui_pool = VK_NULL_HANDLE;
        }

        s_imgui_initialized = false;
        s_imgui_window = nullptr;
        SDL_Log("ImGui shutdown");
    }

    mel_texture_pool_shutdown(s_texture_pool);
    mel_dealloc(&s_allocator, s_texture_pool);
    s_texture_pool = nullptr;

    mel_sprite_pass_shutdown(s_sprite_pass);
    mel_dealloc(&s_allocator, s_sprite_pass);
    s_sprite_pass = nullptr;

    mel_slang_shutdown();
    mel_gpu_submit_shutdown(&s_dev);
    mel_gpu_device_shutdown(&s_dev);

    mel_tracking_report(s_tracking);
    const Mel_Alloc* backing = s_tracking->backing;
    mel_dealloc(backing, s_tracking);
    s_tracking = nullptr;

    s_render_graph = nullptr;
    s_sim_head = nullptr;
    s_initialized = false;
    SDL_Log("Melody Engine shutdown complete");
}

Mel_Gpu_Device* mel_gpu_dev(void)
{
    assert(s_initialized);
    return &s_dev;
}

Mel_Sprite_Pass* mel_sprite_pass(void)
{
    assert(s_initialized);
    return s_sprite_pass;
}

Mel_Texture_Pool* mel_texture_pool(void)
{
    assert(s_initialized);
    return s_texture_pool;
}

const Mel_Alloc* mel_allocator(void)
{
    assert(s_initialized);
    return &s_allocator;
}

void mel_set_render_graph(Mel_Render_Graph* graph)
{
    s_render_graph = graph;
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
            sim->next = nullptr;
            return;
        }
        pp = &(*pp)->next;
    }

    assert(false);
}

void mel_frame(void)
{
    if (!s_initialized) return;

    TracyCZoneN(ctx_iterate, "engine_frame", true);

    u64 now = SDL_GetPerformanceCounter();
    u64 freq = SDL_GetPerformanceFrequency();
    f32 frame_time = (f32)(now - s_last_time) / (f32)freq;
    s_last_time = now;

    if (frame_time > s_max_frame_time)
        frame_time = s_max_frame_time;

    s_frame_dt = frame_time;

    {
        TracyCZoneN(ctx_sims, "tick_simulations", true);
        for (Mel_Sim_Ctx* sim = s_sim_head; sim; sim = sim->next)
            mel_sim_tick(sim, frame_time);
        TracyCZoneEnd(ctx_sims);
    }

    if (s_imgui_initialized)
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        igNewFrame();
    }

    if (s_render_graph)
    {
        TracyCZoneN(ctx_graph, "render_graph_execute", true);
        mel_render_graph_execute(s_render_graph);
        TracyCZoneEnd(ctx_graph);
    }

    if (s_imgui_initialized)
    {
        ImGuiIO* io = igGetIO_Nil();
        if (io->ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            igUpdatePlatformWindows();
            igRenderPlatformWindowsDefault(nullptr, nullptr);
        }
    }

    s_frame_count++;
    TracyCZoneEnd(ctx_iterate);

    TracyCFrameMark;
}

void mel_process_event(SDL_Event* event)
{
    if (s_imgui_initialized)
        ImGui_ImplSDL3_ProcessEvent(event);
}

void mel__engine_init(void)
{
    mel_backtrace_init();
}

void mel__engine_shutdown(void)
{
}
