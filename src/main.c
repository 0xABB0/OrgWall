#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <tracy/TracyC.h>

#include "melody.h"
#include "backtrace.h"
#include "vk_swapchain.h"
#include "vk_shader.h"
#include "vk_pipeline.h"
#include "vk_buffer.h"
#include "vk_texture.h"
#include "sprite_batch.h"
#include "font.h"
#include "ui_panel.h"
#include "ui_label.h"
#include "game.h"
#include "assets.h"
#include "texture.h"
#include "asset_registry.h"
#include "editor.h"

static Mel_Engine s_engine;
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

static Mel_Panel s_dialogue_box;
static Mel_Label s_dialogue_label;

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

static void on_init(Mel_Engine* engine)
{
    if (!mel_vk_shader_init(&s_shader, &engine->vk, .source = SHADER_SOURCE))
    {
        SDL_Log("Failed to compile shader!");
        return;
    }

    if (!mel_vk_texture_init_white(&s_white_texture, &engine->vk))
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

    if (!mel_vk_pipeline_init(&s_pipeline, &engine->vk,
        .shader = &s_shader,
        .swapchain = &engine->swapchain,
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

    if (!mel_sprite_batch_init(&s_batch, &engine->vk, .max_sprites = 1024))
    {
        SDL_Log("Failed to create sprite batch!");
        return;
    }

    s_white_texture.descriptor = mel_vk_pipeline_alloc_descriptor(&s_pipeline, &engine->vk);
    mel_vk_pipeline_write_texture(&s_pipeline, &engine->vk, s_white_texture.descriptor,
        s_white_texture.image.view, s_white_texture.sampler);

    if (!mel_font_init(&s_font, &engine->vk, .path = "/System/Library/Fonts/Monaco.ttf", .size = 20.0f))
    {
        SDL_Log("Failed to load font!");
        return;
    }

    s_font.atlas.descriptor = mel_vk_pipeline_alloc_descriptor(&s_pipeline, &engine->vk);
    mel_vk_pipeline_write_texture(&s_pipeline, &engine->vk, s_font.atlas.descriptor,
        s_font.atlas.image.view, s_font.atlas.sampler);

    if (mel_texture_load_and_bind(&s_test_texture, &engine->vk, &s_pipeline, "test.png"))
    {
        SDL_Log("Loaded test.png via PhysFS!");
    }

    mel_game_init(&s_game);

    mel_panel_init(&s_dialogue_box);
    s_dialogue_box.base.pos = mel_vec2(100, 350);
    s_dialogue_box.base.size = mel_vec2(440, 100);
    s_dialogue_box.base.color = mel_vec4(0.1f, 0.1f, 0.15f, 0.9f);
    s_dialogue_box.base.visible = false;

    mel_label_init(&s_dialogue_label);
    s_dialogue_label.base.pos = mel_vec2(120, 370);
    s_dialogue_label.font = &s_font;
    s_dialogue_label.text = s_game.dialogue_text;
    s_dialogue_label.text_color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    mel_widget_add_child(&s_dialogue_box.base, &s_dialogue_label.base);

    mel_asset_registry_init(&s_asset_registry, &engine->vk, &s_pipeline, mel_engine_allocator(engine));
    mel_asset_registry_scan_directory(&s_asset_registry, nullptr);

    if (!mel_editor_init(&s_editor,
        .window = engine->window,
        .vk = &engine->vk,
        .swapchain = &engine->swapchain,
        .alloc = mel_engine_allocator(engine)))
    {
        SDL_Log("Failed to init editor!");
    }

    mel_editor_set_ecs_world(&s_editor, s_game.ecs.world);
    mel_editor_set_asset_registry(&s_editor, &s_asset_registry);

    SDL_Log("Game ready! WASD to move, E to interact with NPC, F1 for editor");
}

static void on_shutdown(Mel_Engine* engine)
{
    mel_editor_shutdown(&s_editor, &engine->vk);
    mel_asset_registry_shutdown(&s_asset_registry);
    mel_widget_destroy(&s_dialogue_box.base);
    mel_game_shutdown(&s_game);
    mel_sprite_batch_shutdown(&s_batch, &engine->vk);
    mel_font_shutdown(&s_font, &engine->vk);
    mel_vk_texture_shutdown(&s_test_texture, &engine->vk);
    mel_vk_texture_shutdown(&s_white_texture, &engine->vk);
    mel_vk_pipeline_shutdown(&s_pipeline, &engine->vk);
    mel_vk_shader_shutdown(&s_shader, &engine->vk);
    mel_vk_texture_cleanup_immediate(&engine->vk);
    SDL_Log("Game shutdown");
}

static void on_event(Mel_Engine* engine, SDL_Event* event)
{
    MEL_UNUSED(engine);

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

static void on_update(Mel_Engine* engine, f32 dt)
{
    MEL_UNUSED(engine);
    mel_game_update(&s_game, dt);
    s_dialogue_box.base.visible = s_game.dialogue_open;
}

static void on_render(Mel_Engine* engine, f32 alpha)
{
    TracyCZoneN(ctx, "on_render_callback", true);

    if (!s_pipeline.pipeline)
    {
        mel_vk_swapchain_clear(&engine->swapchain, &engine->vk, 1.0f, 0.0f, 1.0f, 1.0f);
        TracyCZoneEnd(ctx);
        return;
    }

    TracyCZoneN(ctx_begin_rendering, "begin_rendering", true);
    mel_vk_swapchain_begin_rendering(&engine->swapchain, &engine->vk, 0.1f, 0.1f, 0.12f, 1.0f);
    TracyCZoneEnd(ctx_begin_rendering);

    VkCommandBuffer cmd = mel_vk_swapchain_command_buffer(&engine->swapchain);

    Mel_Mat4 proj = mel_mat4_ortho(0, (f32)engine->swapchain.extent.width,
                                    0, (f32)engine->swapchain.extent.height, -1, 1);

    mel_sprite_batch_begin(&s_batch, &s_pipeline);
    mel_sprite_batch_set_texture(&s_batch, &engine->vk, &s_white_texture);

    mel_game_draw(&s_game, &s_batch);

    if (s_dialogue_box.base.visible)
    {
        TracyCZoneN(ctx_widget, "widget_draw", true);
        mel_widget_draw(&s_dialogue_box.base, &s_batch);
        TracyCZoneEnd(ctx_widget);
    }

    mel_sprite_batch_end(&s_batch, &engine->vk, cmd, &proj);

    mel_editor_begin_frame(&s_editor, engine->frame_dt);
    mel_editor_end_frame(&s_editor, cmd);

    TracyCZoneN(ctx_end_rendering, "end_rendering", true);
    mel_vk_swapchain_end_rendering(&engine->swapchain, &engine->vk);
    TracyCZoneEnd(ctx_end_rendering);

    MEL_UNUSED(alpha);
    TracyCZoneEnd(ctx);
}

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
    MEL_UNUSED(argc);
    MEL_UNUSED(argv);

    mel_backtrace_init();

    if (!mel_assets_init(&s_assets))
    {
        SDL_Log("Failed to init assets system!");
        return SDL_APP_FAILURE;
    }

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_Log("Failed to init SDL: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!SDL_Vulkan_LoadLibrary("/opt/homebrew/lib/libvulkan.dylib"))
    {
        SDL_Log("Failed to load Vulkan library: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!mel_engine_init(&s_engine,
        .title = "Red Square Room",
        .width = 640,
        .height = 480,
        .on_init = on_init,
        .on_shutdown = on_shutdown,
        .on_update = on_update,
        .on_render = on_render,
        .on_event = on_event))
    {
        return SDL_APP_FAILURE;
    }

    *appstate = &s_engine;
    SDL_Log("Melody Engine initialized!");
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    Mel_Engine* engine = (Mel_Engine*)appstate;

    if (mel_engine_handle_event(engine, event))
    {
        return SDL_APP_SUCCESS;
    }

    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    Mel_Engine* engine = (Mel_Engine*)appstate;

    mel_engine_iterate(engine);

    TracyCFrameMark;

    if (engine->should_quit)
    {
        return SDL_APP_SUCCESS;
    }

    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    MEL_UNUSED(result);

    if (appstate)
    {
        Mel_Engine* engine = (Mel_Engine*)appstate;
        mel_engine_shutdown(engine);
    }

    mel_assets_shutdown(&s_assets);

    SDL_Quit();
    SDL_Log("Melody Engine shutdown complete");
}
