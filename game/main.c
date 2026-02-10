#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <tracy/TracyC.h>
#include <stdlib.h>
#include <string.h>

#define CIMGUI_USE_SDL3
#define CIMGUI_USE_VULKAN
#include <volk.h>
#include <cimgui/cimgui.h>
#include <cimgui/cimgui_impl.h>

#include "app.h"
#include "engine.h"
#include "string.str8.h"
#include "allocator.tracking.h"
#include "allocator.heap.h"
#include "gpu.device.h"
#include "gpu.swapchain.h"
#include "gpu.shader.h"
#include "gpu.pipeline.h"
#include "gpu.buffer.h"
#include "gpu.texture.h"
#include "gpu.cmd.h"
#include "render.frame.h"
#include "sprite_batch.h"
#include "font.atlas.h"
#include "texture.pool.h"
#include "tile.set.h"
#include "tile.map.h"
#include "ui.widget.panel.h"
#include "ui.widget.label.h"
#include "game.h"
#include "assets.h"
#include "texture.h"
#include "editor.h"
#include "editor.registry.h"
#include "editor.tiles.h"
#include "editor.spritesheet.h"
#include "editor.entities.h"
#include "editor.sprite.h"
#include "editor.collider.h"
#include "editor.transform.h"
#include "ecs.2d.transform.h"
#include "ecs.2d.sprite.h"
#include "ecs.2d.collider.h"
#include "player.h"
#include "npc.h"
#include "wall.h"

#define FIXED_DT (1.0f / 60.0f)
#define MAX_FRAME_TIME 0.25f

static SDL_Window* s_window;
static Mel_Gpu_Device s_dev;
static Mel_Gpu_Swapchain s_swapchain;
static Mel_Render_Frame s_frame;
static Mel_Tracking_Allocator* s_tracking;
static Mel_Alloc s_allocator;

static f32 s_accumulator;
static u64 s_last_time;
static f32 s_frame_dt;
static u64 s_frame_count;
static bool s_should_quit;
static bool s_resize_requested;

static Mel_Texture_Pool s_texture_pool;
static Mel_Tileset_Pool s_tileset_pool;
static Mel_Tilemap_Pool s_tilemap_pool;
static Mel_Font_Atlas_Pool s_font_pool;
static Mel_Font_Handle s_font_handle;
static Mel_EdRegistry s_ed_registry;
static Mel_GameEditor s_game_editor;
static Mel_Assets s_assets;
static Mel_Gpu_Shader s_shader;
static Mel_Gpu_Pipeline s_pipeline;
static Mel_Gpu_Texture s_white_texture;
static Mel_Gpu_Texture s_test_texture;
static Mel_SpriteBatch s_batch;
static Mel_Game s_game;

static Mel_WPanel s_dialogue_box;
static Mel_WLabel s_dialogue_label;

static VkDescriptorPool s_imgui_pool;
static bool s_imgui_initialized;

static Mel_EdEntities* s_ed_entities;
static Mel_EdTiles* s_ed_tiles;
static Mel_EdSpritesheet* s_ed_spritesheet;

static const char* SHADER_SOURCE =
"struct VSInput\n"
"{\n"
"    float2 position : POSITION;\n"
"    float2 texcoord : TEXCOORD0;\n"
"    float4 color : COLOR0;\n"
"};\n"
"\n"
"struct VSOutput\n"
"{\n"
"    float4 position : SV_Position;\n"
"    float2 texcoord : TEXCOORD0;\n"
"    float4 color : COLOR0;\n"
"};\n"
"\n"
"struct PushConstants\n"
"{\n"
"    float4x4 projection;\n"
"};\n"
"\n"
"[[vk::push_constant]]\n"
"PushConstants push;\n"
"\n"
"[[vk::binding(0, 0)]] Sampler2D tex;\n"
"\n"
"[shader(\"vertex\")]\n"
"VSOutput vertexMain(VSInput input)\n"
"{\n"
"    VSOutput output;\n"
"    output.position = mul(push.projection, float4(input.position, 0.0, 1.0));\n"
"    output.texcoord = input.texcoord;\n"
"    output.color = input.color;\n"
"    return output;\n"
"}\n"
"\n"
"[shader(\"fragment\")]\n"
"float4 fragmentMain(VSOutput input) : SV_Target\n"
"{\n"
"    return tex.Sample(input.texcoord) * input.color;\n"
"}\n";

static bool imgui_init(SDL_Window* window, Mel_Gpu_Device* dev, Mel_Gpu_Swapchain* swapchain)
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

    if (vkCreateDescriptorPool(dev->device, &pool_info, nullptr, &s_imgui_pool) != VK_SUCCESS)
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

    s_imgui_initialized = true;
    SDL_Log("ImGui initialized");
    return true;
}

static void imgui_shutdown(Mel_Gpu_Device* dev)
{
    if (!s_imgui_initialized) return;

    vkDeviceWaitIdle(dev->device);

    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    igDestroyContext(nullptr);

    if (s_imgui_pool)
    {
        vkDestroyDescriptorPool(dev->device, s_imgui_pool, nullptr);
        s_imgui_pool = VK_NULL_HANDLE;
    }

    s_imgui_initialized = false;
    SDL_Log("ImGui shutdown");
}

static void imgui_process_event(SDL_Event* event)
{
    if (!s_imgui_initialized) return;
    ImGui_ImplSDL3_ProcessEvent(event);
}

static void imgui_begin_frame(void)
{
    if (!s_imgui_initialized) return;

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    igNewFrame();
}

static void imgui_end_frame(VkCommandBuffer cmd)
{
    if (!s_imgui_initialized) return;

    igRender();
    ImGui_ImplVulkan_RenderDrawData(igGetDrawData(), cmd, VK_NULL_HANDLE);

    ImGuiIO* io = igGetIO_Nil();
    if (io->ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        igUpdatePlatformWindows();
        igRenderPlatformWindowsDefault(nullptr, nullptr);
    }
}

static void ed_entities_draw_wrapper(void* instance, f32 dt)
{
    mel_ed_entities_draw((Mel_EdEntities*)instance, dt);
}

static void ed_entities_shutdown_wrapper(void* instance)
{
    mel_ed_entities_shutdown((Mel_EdEntities*)instance);
    mel_dealloc(&s_allocator, instance);
}

static void ed_tiles_draw_wrapper(void* instance, f32 dt)
{
    MEL_UNUSED(dt);
    mel_ed_tiles_draw((Mel_EdTiles*)instance);
}

static void ed_tiles_shutdown_wrapper(void* instance)
{
    mel_ed_tiles_shutdown((Mel_EdTiles*)instance);
    mel_dealloc(&s_allocator, instance);
}

static void ed_spritesheet_draw_wrapper(void* instance, f32 dt)
{
    Mel_EdSpritesheet* ed = (Mel_EdSpritesheet*)instance;

    if (!ed->spritesheet)
    {
        igText("Spritesheet editor (no sheet loaded)");
        igTextDisabled("Load or create a spritesheet via atlas files");
    }
    else
    {
        mel_ed_spritesheet_draw(ed, dt);
    }
}

static void ed_spritesheet_shutdown_wrapper(void* instance)
{
    mel_ed_spritesheet_shutdown((Mel_EdSpritesheet*)instance);
    mel_dealloc(&s_allocator, instance);
}

static void on_init(void)
{
    mel_gpu_shader_init(&s_shader, &s_dev, .source = str8_from_cstr(SHADER_SOURCE));

    mel_gpu_texture_init_white(&s_white_texture, &s_dev);

    VkVertexInputBindingDescription binding = {
        .binding = 0,
        .stride = sizeof(Mel_SpriteVertex),
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    };

    VkVertexInputAttributeDescription attributes[] = {
        { .location = 0, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Mel_SpriteVertex, x) },
        { .location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Mel_SpriteVertex, u) },
        { .location = 2, .binding = 0, .format = VK_FORMAT_R32G32B32A32_SFLOAT, .offset = offsetof(Mel_SpriteVertex, r) },
    };

    mel_gpu_pipeline_init(&s_pipeline, &s_dev,
        .shader = &s_shader,
        .color_format = s_swapchain.format,
        .bindings = &binding,
        .binding_count = 1,
        .attributes = attributes,
        .attribute_count = 3,
        .push_constant_size = sizeof(Mel_Mat4),
        .use_texture = true,
        .blend_mode = MEL_GPU_BLEND_ALPHA);

    mel_sprite_batch_init(&s_batch, &s_dev, .max_sprites = 1024);

    s_white_texture.descriptor = mel_gpu_pipeline_alloc_descriptor(&s_pipeline, &s_dev);
    mel_gpu_pipeline_write_texture(&s_pipeline, &s_dev, s_white_texture.descriptor,
        s_white_texture.image.view, s_white_texture.sampler);

    mel_texture_pool_init(&s_texture_pool, &s_allocator, &s_dev, .pipeline = &s_pipeline);
    mel_tileset_pool_init(&s_tileset_pool, &s_allocator, &s_texture_pool);
    mel_tilemap_pool_init(&s_tilemap_pool, &s_allocator, &s_tileset_pool);
    mel_font_atlas_pool_init(&s_font_pool, &s_allocator, &s_dev);

    s_font_handle = mel_font_atlas_pool_load(&s_font_pool, .path = S8("/System/Library/Fonts/Monaco.ttf"), .size = 20.0f);
    Mel_Font_Atlas_Entry* font_entry = mel_font_atlas_pool_get(&s_font_pool, s_font_handle);
    if (font_entry)
    {
        font_entry->atlas_texture.descriptor = mel_gpu_pipeline_alloc_descriptor(&s_pipeline, &s_dev);
        mel_gpu_pipeline_write_texture(&s_pipeline, &s_dev, font_entry->atlas_texture.descriptor,
            font_entry->atlas_texture.image.view, font_entry->atlas_texture.sampler);
    }

    mel_texture_load_and_bind(&s_test_texture, &s_dev, &s_pipeline, S8("test.png"));

    mel_game_init(&s_game);

    mel_wpanel_init(&s_dialogue_box);
    mel_widget_set_position(&s_dialogue_box.base, mel_vec2(100, 350));
    mel_widget_set_size(&s_dialogue_box.base, mel_vec2(440, 100));
    s_dialogue_box.color = mel_vec4(0.1f, 0.1f, 0.15f, 0.9f);
    mel_widget_set_visible(&s_dialogue_box.base, false);

    mel_wlabel_init(&s_dialogue_label);
    mel_widget_set_position(&s_dialogue_label.base, mel_vec2(120, 370));
    s_dialogue_label.font = font_entry;
    s_dialogue_label.text = str8_from_cstr(s_game.dialogue_text);
    s_dialogue_label.text_color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    mel_widget_add_child(&s_dialogue_box.base, &s_dialogue_label.base);

    if (!imgui_init(s_window, &s_dev, &s_swapchain))
    {
        SDL_Log("Failed to init ImGui!");
    }

    mel_ed_registry_init(&s_ed_registry, .alloc = &s_allocator);

    s_ed_entities = mel_alloc_type(&s_allocator, Mel_EdEntities);
    mel_ed_entities_init(s_ed_entities, &s_allocator);
    mel_ed_entities_set_world(s_ed_entities, s_game.ecs.world);
    mel_ed_entities_register_inspector(s_ed_entities, mel_ed_transform_draw);
    mel_ed_entities_register_inspector(s_ed_entities, mel_editor_sprite);
    mel_ed_entities_register_inspector(s_ed_entities, mel_ed_collider_draw);
    mel_ed_entities_register_inspector(s_ed_entities, mel_ed_player_draw);
    mel_ed_entities_register_inspector(s_ed_entities, mel_ed_npc_draw);
    mel_ed_entities_register_inspector(s_ed_entities, mel_ed_wall_draw);
    mel_ed_registry_add(&s_ed_registry,
        .name = S8("Entity Editor"),
        .data = s_ed_entities,
        .draw = ed_entities_draw_wrapper,
        .shutdown = ed_entities_shutdown_wrapper);

    s_ed_tiles = mel_alloc_type(&s_allocator, Mel_EdTiles);
    mel_ed_tiles_init(s_ed_tiles, &s_allocator);
    mel_ed_tiles_set_pools(s_ed_tiles, &s_tileset_pool, &s_tilemap_pool, &s_texture_pool);

    s_ed_spritesheet = mel_alloc_type(&s_allocator, Mel_EdSpritesheet);
    mel_ed_spritesheet_init(s_ed_spritesheet, &s_allocator);
    s_ed_spritesheet->texture_pool = &s_texture_pool;

    mel_game_editor_init(&s_game_editor, &s_ed_registry, &s_texture_pool, &s_tileset_pool, &s_tilemap_pool, s_window);

    SDL_Log("Game ready! WASD to move, E to interact with NPC, F1 for editor");
}

static void on_shutdown(void)
{
    mel_game_editor_shutdown(&s_game_editor);
    mel_ed_registry_shutdown(&s_ed_registry);

    if (s_ed_tiles)
    {
        mel_ed_tiles_shutdown(s_ed_tiles);
        mel_dealloc(&s_allocator, s_ed_tiles);
        s_ed_tiles = nullptr;
    }
    if (s_ed_spritesheet)
    {
        mel_ed_spritesheet_shutdown(s_ed_spritesheet);
        mel_dealloc(&s_allocator, s_ed_spritesheet);
        s_ed_spritesheet = nullptr;
    }

    imgui_shutdown(&s_dev);
    mel_widget_destroy(&s_dialogue_box.base);
    mel_game_shutdown(&s_game);
    mel_sprite_batch_shutdown(&s_batch, &s_dev);
    mel_font_atlas_pool_shutdown(&s_font_pool);
    mel_tilemap_pool_shutdown(&s_tilemap_pool);
    mel_tileset_pool_shutdown(&s_tileset_pool);
    mel_texture_pool_shutdown(&s_texture_pool);
    mel_gpu_texture_shutdown(&s_test_texture, &s_dev);
    mel_gpu_texture_shutdown(&s_white_texture, &s_dev);
    mel_gpu_pipeline_shutdown(&s_pipeline, &s_dev);
    mel_gpu_shader_shutdown(&s_shader, &s_dev);
    SDL_Log("Game shutdown");
}

static void on_event(SDL_Event* event)
{
    imgui_process_event(event);

    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat)
    {
        if (event->key.scancode == SDL_SCANCODE_F1)
        {
            mel_game_editor_toggle(&s_game_editor);
        }
    }

    mel_ed_registry_process_event(&s_ed_registry, event);
    mel_game_handle_event(&s_game, event);
}

static void on_update(f32 dt)
{
    mel_game_update(&s_game, dt);
    mel_widget_set_visible(&s_dialogue_box.base, s_game.dialogue_open);
}

static void on_render(Mel_Gpu_Cmd* c, f32 alpha)
{
    TracyCZoneN(ctx, "on_render_callback", true);

    if (!s_pipeline.pipeline)
    {
        TracyCZoneEnd(ctx);
        return;
    }

    mel_gpu_cmd_image_barrier(c,
        s_swapchain.images[s_swapchain.current_image],
        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_IMAGE_ASPECT_COLOR_BIT);

    mel_gpu_cmd_set_viewport(c, 0, 0,
        (f32)s_swapchain.extent.width, (f32)s_swapchain.extent.height, 0, 1);
    mel_gpu_cmd_set_scissor(c, 0, 0, s_swapchain.extent.width, s_swapchain.extent.height);

    Mel_Gpu_Color_Attachment color_att = {
        .image_view = s_swapchain.image_views[s_swapchain.current_image],
        .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .clear_r = 0.1f, .clear_g = 0.1f, .clear_b = 0.12f, .clear_a = 1.0f,
    };

    mel_gpu_cmd_begin_rendering(c,
        .color_attachments = &color_att,
        .color_count = 1,
        .render_width = s_swapchain.extent.width,
        .render_height = s_swapchain.extent.height);

    Mel_Mat4 proj = mel_mat4_ortho(0, (f32)s_swapchain.extent.width,
                                    0, (f32)s_swapchain.extent.height, -1, 1);

    mel_sprite_batch_begin(&s_batch, &s_pipeline);
    mel_sprite_batch_set_texture(&s_batch, &s_dev, &s_white_texture);

    mel_game_draw(&s_game, &s_batch);

    if (s_dialogue_box.base.visible)
    {
        TracyCZoneN(ctx_widget, "widget_draw", true);
        mel_widget_draw(&s_dialogue_box.base, &s_batch);
        TracyCZoneEnd(ctx_widget);
    }

    mel_sprite_batch_end(&s_batch, &s_dev, c->cmd, &proj);

    TracyCZoneN(ctx_editor, "editor_frame", true);
    if (s_game_editor.visible)
    {
        imgui_begin_frame();
        mel_game_editor_draw(&s_game_editor, s_frame_dt);
        imgui_end_frame(c->cmd);
    }
    TracyCZoneEnd(ctx_editor);

    mel_gpu_cmd_end_rendering(c);

    mel_gpu_cmd_image_barrier(c,
        s_swapchain.images[s_swapchain.current_image],
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_IMAGE_ASPECT_COLOR_BIT);

    MEL_UNUSED(alpha);
    TracyCZoneEnd(ctx);
}

static void app_init(Mel_App* app)
{
    MEL_UNUSED(app);

    if (!mel_assets_init(&s_assets))
    {
        SDL_Log("Failed to init assets system!");
        return;
    }

    if (!SDL_Vulkan_LoadLibrary("/opt/homebrew/lib/libvulkan.dylib"))
    {
        SDL_Log("Failed to load Vulkan library: %s", SDL_GetError());
        return;
    }

    s_tracking = (Mel_Tracking_Allocator*)malloc(sizeof(Mel_Tracking_Allocator));
    mel_tracking_init(s_tracking, mel_alloc_heap());
    s_allocator = mel_tracking_allocator(s_tracking);

    s_window = SDL_CreateWindow("Red Square Room", 640, 480,
                                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!s_window)
    {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        return;
    }

    mel_gpu_device_init(&s_dev,
        .window = s_window,
        .allocator = &s_allocator,
        .enable_validation = true,
        .app_name = S8("Red Square Room"));

    mel_gpu_swapchain_init(&s_swapchain, &s_dev, .width = 640, .height = 480);

    mel_slang_init();

    mel_render_frame_init(&s_frame, .dev = &s_dev, .swapchain = &s_swapchain);

    s_last_time = SDL_GetPerformanceCounter();

    on_init();

    SDL_Log("Melody Engine initialized!");
}

static void app_shutdown(Mel_App* app)
{
    MEL_UNUSED(app);

    mel_gpu_device_wait_idle(&s_dev);
    on_shutdown();

    mel_render_frame_shutdown(&s_frame);
    mel_slang_shutdown();
    mel_gpu_swapchain_shutdown(&s_swapchain, &s_dev);
    mel_gpu_device_shutdown(&s_dev);

    if (s_window)
    {
        SDL_DestroyWindow(s_window);
        s_window = nullptr;
    }

    mel_tracking_report(s_tracking);
    free(s_tracking);
    s_tracking = nullptr;

    mel_assets_shutdown(&s_assets);
    SDL_Log("Melody Engine shutdown complete");
}

static void app_update(Mel_App* app)
{
    TracyCZoneN(ctx_iterate, "app_update", true);

    SDL_WindowFlags flags = SDL_GetWindowFlags(s_window);
    if (flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN | SDL_WINDOW_OCCLUDED))
    {
        SDL_Delay(16);
        TracyCZoneEnd(ctx_iterate);
        TracyCFrameMark;
        return;
    }

    u64 now = SDL_GetPerformanceCounter();
    u64 freq = SDL_GetPerformanceFrequency();
    f32 frame_time = (f32)(now - s_last_time) / (f32)freq;
    s_last_time = now;

    if (frame_time > MAX_FRAME_TIME)
        frame_time = MAX_FRAME_TIME;

    s_frame_dt = frame_time;
    s_accumulator += frame_time;

    while (s_accumulator >= FIXED_DT)
    {
        TracyCZoneN(ctx_update, "on_update", true);
        on_update(FIXED_DT);
        s_accumulator -= FIXED_DT;
        TracyCZoneEnd(ctx_update);
    }

    if (s_resize_requested)
    {
        i32 w, h;
        SDL_GetWindowSize(s_window, &w, &h);
        if (w > 0 && h > 0)
            mel_gpu_swapchain_recreate(&s_swapchain, &s_dev, w, h);
        s_resize_requested = false;
    }

    TracyCZoneN(ctx_acquire, "frame_begin", true);
    if (!mel_render_frame_begin(&s_frame))
    {
        i32 w, h;
        SDL_GetWindowSize(s_window, &w, &h);
        if (w > 0 && h > 0)
            mel_gpu_swapchain_recreate(&s_swapchain, &s_dev, w, h);
        TracyCZoneEnd(ctx_acquire);
        TracyCZoneEnd(ctx_iterate);
        TracyCFrameMark;
        return;
    }
    TracyCZoneEnd(ctx_acquire);

    Mel_Gpu_Cmd c = {
        .cmd = mel_render_frame_cmd(&s_frame),
        .dev = &s_dev,
    };

    f32 alpha = s_accumulator / FIXED_DT;

    TracyCZoneN(ctx_render, "on_render", true);
    on_render(&c, alpha);
    TracyCZoneEnd(ctx_render);

    TracyCZoneN(ctx_present, "frame_end", true);
    mel_render_frame_end(&s_frame);
    TracyCZoneEnd(ctx_present);

    s_frame_count++;
    TracyCZoneEnd(ctx_iterate);

    TracyCFrameMark;

    if (s_should_quit)
        app->should_quit = true;
}

static void app_event(Mel_App* app, SDL_Event* event)
{
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE)
    {
        app->should_quit = true;
        return;
    }

    if (event->type == SDL_EVENT_WINDOW_RESIZED &&
        event->window.windowID == SDL_GetWindowID(s_window))
    {
        s_resize_requested = true;
    }

    on_event(event);
}

MEL_APP(
    .on_init = app_init,
    .on_shutdown = app_shutdown,
    .on_update = app_update,
    .on_event = app_event
)
