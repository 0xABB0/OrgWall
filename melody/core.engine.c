#include "core.engine.h"
#include "render.graph.h"
#include "sim.ctx.h"
#include "gpu.device.h"
#include "gpu.swapchain.h"
#include "swapchain.h"
#include "window.h"
#include "window.present.2d.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "font.atlas.h"
#include "gpu.shader.h"
#include "gpu.submit.h"
#include "mesh.pass.h"
#include "sprite.pass.h"
#include "stage.h"
#include "text.pass.h"
#include "texture.pool.h"
#include "string.str8.h"
#include "boot.registry.h"
#include "async.job.h"
#include "async.signal.h"
#include "event.channel.h"
#include "thread.dispatch.h"

#include <stdatomic.h>

#include <tracy/TracyC.h>

#define CIMGUI_USE_SDL3
#define CIMGUI_USE_VULKAN
#include <cimgui/cimgui.h>
#include <cimgui/cimgui_impl.h>

#ifndef MEL_GPU_VALIDATION
#ifdef NDEBUG
#define MEL_GPU_VALIDATION 0
#else
#define MEL_GPU_VALIDATION 1
#endif
#endif

#ifndef MEL_MAX_FRAME_TIME
#define MEL_MAX_FRAME_TIME 0.25f
#endif

#ifndef MEL_APP_NAME
#define MEL_APP_NAME "Melody"
#endif

static Mel_Render_Graph* s_render_graph;
static Mel_Sim_Ctx* s_sim_head;
static Mel_Frame_Stats s_frame_stats;
static f32 s_fps_accum;
static u32 s_fps_frames;
static u64 s_last_time;
static u64 s_frame_count;
static bool s_initialized;

static VkDescriptorPool s_imgui_pool;
static Mel_Window_Handle s_imgui_window;
static bool s_imgui_initialized;

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
        .enable_validation = MEL_GPU_VALIDATION,
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

bool mel_imgui_init(Mel_Window_Handle window, Mel_Swapchain* swapchain)
{
    assert(mel_window_handle_valid(window));
    assert(swapchain != nullptr);

    Mel_Gpu_Device* dev = mel_gpu_dev();

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

    if (vkCreateDescriptorPool(dev->device, &pool_info, NULL, &s_imgui_pool) != VK_SUCCESS)
    {
        SDL_Log("Failed to create ImGui descriptor pool");
        return false;
    }

    igCreateContext(NULL);

    ImGuiIO* io = igGetIO_Nil();
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io->ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;

    igStyleColorsDark(NULL);

    ImGuiStyle* style = igGetStyle();
    if (io->ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style->WindowRounding = 0.0f;
        style->Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplSDL3_InitForVulkan(mel__window_sdl(window));

    VkPipelineRenderingCreateInfoKHR rendering_info = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &swapchain->format,
    };

    ImGui_ImplVulkan_InitInfo init_info = {
        .ApiVersion = VK_API_VERSION_1_3,
        .Instance = dev->instance,
        .PhysicalDevice = dev->physical_device,
        .Device = dev->device,
        .QueueFamily = dev->graphics_family,
        .Queue = dev->graphics_queue,
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

void mel_shutdown(void)
{
    if (!s_initialized) return;

    Mel_Gpu_Device* dev = mel_gpu_dev();
    vkDeviceWaitIdle(dev->device);
    mel_stage_shutdown_all();
    mel__window_present_2d_shutdown_all();

    if (s_imgui_initialized)
    {
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        igDestroyContext(NULL);

        if (s_imgui_pool)
        {
            vkDestroyDescriptorPool(dev->device, s_imgui_pool, NULL);
            s_imgui_pool = VK_NULL_HANDLE;
        }

        s_imgui_initialized = false;
        s_imgui_window = MEL_WINDOW_HANDLE_NULL;
        SDL_Log("ImGui shutdown");
    }

    mel_font_atlas_pool_shutdown(mel_font_pool());
    mel_texture_pool_shutdown(mel_texture_pool());
    mel_mesh_pass_shutdown(mel_mesh_pass());
    mel_text_pass_shutdown(mel_text_pass());
    mel_sprite_pass_shutdown(mel_sprite_pass());

    mel_swapchain_registry_destroy_all(dev);

    mel_slang_shutdown();
    mel_gpu_submit_shutdown(dev);
    mel_gpu_device_shutdown(dev);

    s_render_graph = NULL;
    s_sim_head = NULL;
    s_initialized = false;

    mel_job_shutdown();
    mel__main_dispatch_shutdown();

    SDL_Log("Melody Engine shutdown complete");
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

    {
        TracyCZoneN(ctx_present, "tick_presentations", true);
        mel__window_present_2d_tick();
        TracyCZoneEnd(ctx_present);
    }

    if (s_imgui_initialized)
    {
        TracyCZoneN(ctx_imgui_begin, "imgui_new_frame", true);
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        igNewFrame();
        TracyCZoneEnd(ctx_imgui_begin);
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
            TracyCZoneN(ctx_imgui_viewports, "imgui_platform_windows", true);
            igUpdatePlatformWindows();
            igRenderPlatformWindowsDefault(NULL, NULL);
            TracyCZoneEnd(ctx_imgui_viewports);
        }
    }

    s_frame_count++;
    s_frame_stats.frame_count = s_frame_count;
    TracyCZoneEnd(ctx_iterate);

    TracyCFrameMark;
}

void mel_process_event(SDL_Event* event)
{
    mel__window_present_2d_process_event(event);
    if (s_imgui_initialized)
        ImGui_ImplSDL3_ProcessEvent(event);
}

Mel_Frame_Stats mel_frame_stats(void)
{
    return s_frame_stats;
}

