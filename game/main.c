#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <tracy/TracyC.h>
#include <stdlib.h>

#include "app.h"
#include "engine.h"
#include "allocator.tracking.h"
#include "allocator.heap.h"
#include "vk_context.h"
#include "vk_swapchain.h"
#include "vk_shader.h"
#include "vk_pipeline.h"
#include "vk_buffer.h"
#include "vk_texture.h"
#include "sprite_batch.h"
#include "font.h"
#include "ui.widget.panel.h"
#include "ui.widget.label.h"
#include "game.h"
#include "assets.h"
#include "texture.h"
#include "asset_registry.h"
#include "editor.h"

#define FIXED_DT (1.0f / 60.0f)
#define MAX_FRAME_TIME 0.25f

static SDL_Window* s_window;
static Mel_VkContext s_vk;
static Mel_VkSwapchain s_swapchain;
static Mel_Tracking_Allocator* s_tracking;
static Mel_Alloc s_allocator;

static f32 s_accumulator;
static u64 s_last_time;
static f32 s_frame_dt;
static u64 s_frame_count;
static bool s_should_quit;
static bool s_resize_requested;

static Mel_AssetRegistry s_asset_registry;
static Mel_Editor s_editor;
static Mel_Assets s_assets;
static Mel_VkShader s_shader;
static Mel_VkPipeline s_pipeline;
static Mel_VkTexture s_white_texture;
static Mel_VkTexture s_test_texture;
static Mel_SpriteBatch s_batch;
static Mel_Font s_font;
static Mel_Game s_game;

static Mel_WPanel s_dialogue_box;
static Mel_WLabel s_dialogue_label;

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

static void on_init(void)
{
    if (!mel_vk_shader_init(&s_shader, &s_vk, .source = SHADER_SOURCE))
    {
        SDL_Log("Failed to compile shader!");
        return;
    }

    if (!mel_vk_texture_init_white(&s_white_texture, &s_vk))
    {
        SDL_Log("Failed to create white texture!");
        return;
    }

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

    if (!mel_vk_pipeline_init(&s_pipeline, &s_vk,
        .shader = &s_shader,
        .swapchain = &s_swapchain,
        .bindings = &binding,
        .binding_count = 1,
        .attributes = attributes,
        .attribute_count = 3,
        .push_constant_size = sizeof(Mel_Mat4),
        .use_texture = true))
    {
        SDL_Log("Failed to create pipeline!");
        return;
    }

    if (!mel_sprite_batch_init(&s_batch, &s_vk, .max_sprites = 1024))
    {
        SDL_Log("Failed to create sprite batch!");
        return;
    }

    s_white_texture.descriptor = mel_vk_pipeline_alloc_descriptor(&s_pipeline, &s_vk);
    mel_vk_pipeline_write_texture(&s_pipeline, &s_vk, s_white_texture.descriptor,
        s_white_texture.image.view, s_white_texture.sampler);

    if (!mel_font_init(&s_font, &s_vk, .path = "/System/Library/Fonts/Monaco.ttf", .size = 20.0f))
    {
        SDL_Log("Failed to load font!");
        return;
    }

    s_font.atlas.descriptor = mel_vk_pipeline_alloc_descriptor(&s_pipeline, &s_vk);
    mel_vk_pipeline_write_texture(&s_pipeline, &s_vk, s_font.atlas.descriptor,
        s_font.atlas.image.view, s_font.atlas.sampler);

    if (mel_texture_load_and_bind(&s_test_texture, &s_vk, &s_pipeline, "test.png"))
    {
        SDL_Log("Loaded test.png via PhysFS!");
    }

    mel_game_init(&s_game);

    mel_wpanel_init(&s_dialogue_box);
    mel_widget_set_position(&s_dialogue_box.base, mel_vec2(100, 350));
    mel_widget_set_size(&s_dialogue_box.base, mel_vec2(440, 100));
    s_dialogue_box.color = mel_vec4(0.1f, 0.1f, 0.15f, 0.9f);
    mel_widget_set_visible(&s_dialogue_box.base, false);

    mel_wlabel_init(&s_dialogue_label);
    mel_widget_set_position(&s_dialogue_label.base, mel_vec2(120, 370));
    s_dialogue_label.font = &s_font;
    s_dialogue_label.text = s_game.dialogue_text;
    s_dialogue_label.text_color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    mel_widget_add_child(&s_dialogue_box.base, &s_dialogue_label.base);

    mel_asset_registry_init(&s_asset_registry, &s_vk, &s_pipeline, &s_allocator);
    mel_asset_registry_scan_directory(&s_asset_registry, nullptr);

    if (!mel_editor_init(&s_editor,
        .window = s_window,
        .vk = &s_vk,
        .swapchain = &s_swapchain,
        .alloc = &s_allocator))
    {
        SDL_Log("Failed to init editor!");
    }

    mel_editor_set_ecs_world(&s_editor, s_game.ecs.world);
    mel_editor_set_asset_registry(&s_editor, &s_asset_registry);

    SDL_Log("Game ready! WASD to move, E to interact with NPC, F1 for editor");
}

static void on_shutdown(void)
{
    mel_editor_shutdown(&s_editor, &s_vk);
    mel_asset_registry_shutdown(&s_asset_registry);
    mel_widget_destroy(&s_dialogue_box.base);
    mel_game_shutdown(&s_game);
    mel_sprite_batch_shutdown(&s_batch, &s_vk);
    mel_font_shutdown(&s_font, &s_vk);
    mel_vk_texture_shutdown(&s_test_texture, &s_vk);
    mel_vk_texture_shutdown(&s_white_texture, &s_vk);
    mel_vk_pipeline_shutdown(&s_pipeline, &s_vk);
    mel_vk_shader_shutdown(&s_shader, &s_vk);
    mel_vk_texture_cleanup_immediate(&s_vk);
    SDL_Log("Game shutdown");
}

static void on_event(SDL_Event* event)
{
    mel_editor_process_event(&s_editor, event);

    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat)
    {
        if (event->key.scancode == SDL_SCANCODE_F1)
        {
            mel_editor_toggle(&s_editor);
        }
    }

    mel_game_handle_event(&s_game, event);
}

static void on_update(f32 dt)
{
    mel_game_update(&s_game, dt);
    mel_widget_set_visible(&s_dialogue_box.base, s_game.dialogue_open);
}

static void on_render(f32 alpha)
{
    TracyCZoneN(ctx, "on_render_callback", true);

    if (!s_pipeline.pipeline)
    {
        mel_vk_swapchain_clear(&s_swapchain, &s_vk, 1.0f, 0.0f, 1.0f, 1.0f);
        TracyCZoneEnd(ctx);
        return;
    }

    TracyCZoneN(ctx_begin_rendering, "begin_rendering", true);
    mel_vk_swapchain_begin_rendering(&s_swapchain, &s_vk, 0.1f, 0.1f, 0.12f, 1.0f);
    TracyCZoneEnd(ctx_begin_rendering);

    VkCommandBuffer cmd = mel_vk_swapchain_command_buffer(&s_swapchain);

    Mel_Mat4 proj = mel_mat4_ortho(0, (f32)s_swapchain.extent.width,
                                    0, (f32)s_swapchain.extent.height, -1, 1);

    mel_sprite_batch_begin(&s_batch, &s_pipeline);
    mel_sprite_batch_set_texture(&s_batch, &s_vk, &s_white_texture);

    mel_game_draw(&s_game, &s_batch);

    if (s_dialogue_box.base.visible)
    {
        TracyCZoneN(ctx_widget, "widget_draw", true);
        mel_widget_draw(&s_dialogue_box.base, &s_batch);
        TracyCZoneEnd(ctx_widget);
    }

    mel_sprite_batch_end(&s_batch, &s_vk, cmd, &proj);

    mel_editor_begin_frame(&s_editor, s_frame_dt);
    mel_editor_end_frame(&s_editor, cmd);

    TracyCZoneN(ctx_end_rendering, "end_rendering", true);
    mel_vk_swapchain_end_rendering(&s_swapchain, &s_vk);
    TracyCZoneEnd(ctx_end_rendering);

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

    if (!mel_vk_context_init(&s_vk,
        .window = s_window,
        .allocator = &s_allocator,
        .enable_validation = true,
        .app_name = "Red Square Room"))
    {
        SDL_Log("Failed to initialize Vulkan");
        return;
    }

    if (!mel_vk_swapchain_init(&s_swapchain, &s_vk, 640, 480))
    {
        SDL_Log("Failed to create swapchain");
        return;
    }

    if (!mel_slang_init())
    {
        SDL_Log("Failed to initialize Slang");
        return;
    }

    s_last_time = SDL_GetPerformanceCounter();

    on_init();

    SDL_Log("Melody Engine initialized!");
}

static void app_shutdown(Mel_App* app)
{
    MEL_UNUSED(app);

    mel_vk_context_wait_idle(&s_vk);
    on_shutdown();

    mel_slang_shutdown();
    mel_vk_swapchain_shutdown(&s_swapchain, &s_vk);
    mel_vk_context_shutdown(&s_vk);

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
            mel_vk_swapchain_recreate(&s_swapchain, &s_vk, w, h);
        s_resize_requested = false;
    }

    TracyCZoneN(ctx_acquire, "swapchain_acquire", true);
    if (!mel_vk_swapchain_acquire(&s_swapchain, &s_vk))
    {
        i32 w, h;
        SDL_GetWindowSize(s_window, &w, &h);
        if (w > 0 && h > 0)
            mel_vk_swapchain_recreate(&s_swapchain, &s_vk, w, h);
        TracyCZoneEnd(ctx_acquire);
        TracyCZoneEnd(ctx_iterate);
        TracyCFrameMark;
        return;
    }
    TracyCZoneEnd(ctx_acquire);

    f32 alpha = s_accumulator / FIXED_DT;

    TracyCZoneN(ctx_begin_frame, "swapchain_begin_frame", true);
    mel_vk_swapchain_begin_frame(&s_swapchain, &s_vk);
    TracyCZoneEnd(ctx_begin_frame);

    TracyCZoneN(ctx_render, "on_render", true);
    on_render(alpha);
    TracyCZoneEnd(ctx_render);

    TracyCZoneN(ctx_end_frame, "swapchain_end_frame", true);
    mel_vk_swapchain_end_frame(&s_swapchain, &s_vk);
    TracyCZoneEnd(ctx_end_frame);

    TracyCZoneN(ctx_present, "swapchain_present", true);
    if (!mel_vk_swapchain_present(&s_swapchain, &s_vk))
    {
        i32 w, h;
        SDL_GetWindowSize(s_window, &w, &h);
        if (w > 0 && h > 0)
            mel_vk_swapchain_recreate(&s_swapchain, &s_vk, w, h);
    }
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
