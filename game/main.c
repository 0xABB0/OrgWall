#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <tracy/TracyC.h>

#define CIMGUI_USE_SDL3
#define CIMGUI_USE_VULKAN
#include <volk.h>
#include <cimgui/cimgui.h>
#include <cimgui/cimgui_impl.h>

#include "core.app.h"
#include "core.engine.h"
#include "string.str8.h"
#include "gpu.shader.h"
#include "gpu.pipeline.h"
#include "gpu.buffer.h"
#include "gpu.texture.h"
#include "gpu.cmd.h"
#include "sprite.batch.h"
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
#include "tile.editor.h"
#include "sprite.sheet.editor.h"
#include "editor.entities.h"
#include "ecs.2d.sprite.editor.h"
#include "ecs.2d.collider.editor.h"
#include "ecs.2d.transform.editor.h"
#include "ecs.2d.transform.h"
#include "ecs.2d.sprite.h"
#include "ecs.2d.collider.h"
#include "player.h"
#include "npc.h"
#include "wall.h"

static SDL_Window* s_window;

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

static Mel_EdEntities* s_ed_entities;
static Mel_EdTiles* s_ed_tiles;
static Mel_EdSpritesheet* s_ed_spritesheet;

static Mel_Alloc* s_alloc;

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

static void ed_entities_draw_wrapper(void* instance, f32 dt)
{
    mel_ed_entities_draw((Mel_EdEntities*)instance, dt);
}

static void ed_entities_shutdown_wrapper(void* instance)
{
    mel_ed_entities_shutdown((Mel_EdEntities*)instance);
    mel_dealloc(s_alloc, instance);
}

static void ed_tiles_draw_wrapper(void* instance, f32 dt)
{
    MEL_UNUSED(dt);
    mel_ed_tiles_draw((Mel_EdTiles*)instance);
}

static void ed_tiles_shutdown_wrapper(void* instance)
{
    mel_ed_tiles_shutdown((Mel_EdTiles*)instance);
    mel_dealloc(s_alloc, instance);
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
    mel_dealloc(s_alloc, instance);
}

static void on_init(Mel_Engine* e)
{
    s_alloc = &e->allocator;
    Mel_Alloc* alloc = s_alloc;
    Mel_Gpu_Device* dev = &e->dev;

    mel_gpu_shader_init(&s_shader, dev, .source = str8_from_cstr(SHADER_SOURCE));

    mel_gpu_texture_init_white(&s_white_texture, dev);

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

    mel_gpu_pipeline_init(&s_pipeline, dev,
        .shader = &s_shader,
        .color_format = e->swapchain.format,
        .bindings = &binding,
        .binding_count = 1,
        .attributes = attributes,
        .attribute_count = 3,
        .push_constant_size = sizeof(Mel_Mat4),
        .use_texture = true,
        .blend_mode = MEL_GPU_BLEND_ALPHA);

    mel_sprite_batch_init(&s_batch, dev, .max_sprites = 1024);

    s_white_texture.descriptor = mel_gpu_pipeline_alloc_descriptor(&s_pipeline, dev);
    mel_gpu_pipeline_write_texture(&s_pipeline, dev, s_white_texture.descriptor,
        s_white_texture.image.view, s_white_texture.sampler);

    mel_texture_pool_init(&s_texture_pool, alloc, dev, .pipeline = &s_pipeline, .assets = &s_assets);
    mel_tileset_pool_init(&s_tileset_pool, alloc, &s_texture_pool, &s_assets);
    mel_tilemap_pool_init(&s_tilemap_pool, alloc, &s_tileset_pool, &s_assets);
    mel_font_atlas_pool_init(&s_font_pool, alloc, dev);

    s_font_handle = mel_font_atlas_pool_load(&s_font_pool, .path = S8("/System/Library/Fonts/Monaco.ttf"), .size = 20.0f);
    Mel_Font_Atlas_Entry* font_entry = mel_font_atlas_pool_get(&s_font_pool, s_font_handle);
    if (font_entry)
    {
        font_entry->atlas_texture.descriptor = mel_gpu_pipeline_alloc_descriptor(&s_pipeline, dev);
        mel_gpu_pipeline_write_texture(&s_pipeline, dev, font_entry->atlas_texture.descriptor,
            font_entry->atlas_texture.image.view, font_entry->atlas_texture.sampler);
    }

    mel_texture_load_and_bind(&s_test_texture, dev, &s_pipeline, &s_assets, S8("test.png"));

    mel_game_init(&s_game);

    mel_wpanel_init(&s_dialogue_box);
    mel_widget_set_position(&s_dialogue_box.base, mel_vec2(100, 350));
    mel_widget_set_size(&s_dialogue_box.base, mel_vec2(440, 100));
    s_dialogue_box.color = mel_vec4(0.1f, 0.1f, 0.15f, 0.9f);
    mel_widget_set_visible(&s_dialogue_box.base, false);

    mel_wlabel_init(&s_dialogue_label);
    mel_widget_set_position(&s_dialogue_label.base, mel_vec2(120, 370));
    s_dialogue_label.font = s_font_handle;
    s_dialogue_label.font_pool = &s_font_pool;
    s_dialogue_label.text = str8_from_cstr(s_game.dialogue_text);
    s_dialogue_label.text_color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    mel_widget_add_child(&s_dialogue_box.base, &s_dialogue_label.base);

    mel_ed_registry_init(&s_ed_registry, .alloc = alloc);

    s_ed_entities = mel_alloc_type(alloc, Mel_EdEntities);
    mel_ed_entities_init(s_ed_entities, alloc);
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

    s_ed_tiles = mel_alloc_type(alloc, Mel_EdTiles);
    mel_ed_tiles_init(s_ed_tiles, alloc);
    mel_ed_tiles_set_pools(s_ed_tiles, &s_tileset_pool, &s_tilemap_pool, &s_texture_pool);

    s_ed_spritesheet = mel_alloc_type(alloc, Mel_EdSpritesheet);
    mel_ed_spritesheet_init(s_ed_spritesheet, alloc);
    s_ed_spritesheet->texture_pool = &s_texture_pool;

    mel_game_editor_init(&s_game_editor, &s_ed_registry, &s_texture_pool, &s_tileset_pool, &s_tilemap_pool, s_window);

    SDL_Log("Game ready! WASD to move, E to interact with NPC, F1 for editor");
}

static void app_init(Mel_App* app)
{
    if (!mel_assets_init(&s_assets))
    {
        SDL_Log("Failed to init assets system!");
        return;
    }

    s_window = SDL_CreateWindow("Red Square Room", 640, 480,
                                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!s_window)
    {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        return;
    }

    mel_engine_init(&app->engine,
        .window = s_window,
        .app_name = S8("Red Square Room"),
        .enable_validation = true,
        .enable_imgui = true,
        .fixed_dt = 1.0f / 60.0f);

    on_init(&app->engine);
}

static void app_shutdown(Mel_App* app)
{
    Mel_Gpu_Device* dev = &app->engine.dev;

    mel_gpu_device_wait_idle(dev);

    mel_game_editor_shutdown(&s_game_editor);
    mel_ed_registry_shutdown(&s_ed_registry);

    if (s_ed_tiles)
    {
        mel_ed_tiles_shutdown(s_ed_tiles);
        mel_dealloc(s_alloc, s_ed_tiles);
        s_ed_tiles = nullptr;
    }
    if (s_ed_spritesheet)
    {
        mel_ed_spritesheet_shutdown(s_ed_spritesheet);
        mel_dealloc(s_alloc, s_ed_spritesheet);
        s_ed_spritesheet = nullptr;
    }

    mel_widget_destroy(&s_dialogue_box.base);
    mel_game_shutdown(&s_game);
    mel_sprite_batch_shutdown(&s_batch, dev);
    mel_font_atlas_pool_shutdown(&s_font_pool);
    mel_tilemap_pool_shutdown(&s_tilemap_pool);
    mel_tileset_pool_shutdown(&s_tileset_pool);
    mel_texture_pool_shutdown(&s_texture_pool);
    mel_gpu_texture_shutdown(&s_test_texture, dev);
    mel_gpu_texture_shutdown(&s_white_texture, dev);
    mel_gpu_pipeline_shutdown(&s_pipeline, dev);
    mel_gpu_shader_shutdown(&s_shader, dev);

    mel_assets_shutdown(&s_assets);
    SDL_Log("Game shutdown");
}

static void app_update(Mel_App* app, f32 dt)
{
    MEL_UNUSED(app);
    mel_game_update(&s_game, dt);
    mel_widget_set_visible(&s_dialogue_box.base, s_game.dialogue_open);
}

static void app_render(Mel_App* app, Mel_Gpu_Cmd* c)
{
    Mel_Engine* e = &app->engine;
    TracyCZoneN(ctx, "app_render", true);

    if (!s_pipeline.pipeline)
    {
        TracyCZoneEnd(ctx);
        return;
    }

    mel_engine_begin_swapchain_pass(e, c,
        .clear_r = 0.1f, .clear_g = 0.1f, .clear_b = 0.12f, .clear_a = 1.0f);

    Mel_Mat4 proj = mel_mat4_ortho(0, (f32)e->swapchain.extent.width,
                                    0, (f32)e->swapchain.extent.height, -1, 1);

    mel_sprite_batch_begin(&s_batch, &s_pipeline);
    mel_sprite_batch_set_texture(&s_batch, &s_white_texture);

    mel_game_draw(&s_game, &s_batch);

    if (s_dialogue_box.base.visible)
    {
        TracyCZoneN(ctx_widget, "widget_draw", true);
        mel_widget_draw(&s_dialogue_box.base, &s_batch);
        TracyCZoneEnd(ctx_widget);
    }

    mel_sprite_batch_end(&s_batch, &e->dev, c->cmd, &proj);

    TracyCZoneN(ctx_editor, "editor_frame", true);
    if (s_game_editor.visible)
    {
        mel_game_editor_draw(&s_game_editor, e->frame_dt);
    }
    TracyCZoneEnd(ctx_editor);

    mel_engine_end_swapchain_pass(e, c);

    TracyCZoneEnd(ctx);
}

static void app_event(Mel_App* app, SDL_Event* event)
{
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE)
    {
        app->should_quit = true;
        return;
    }

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

MEL_APP(
    .on_init = app_init,
    .on_shutdown = app_shutdown,
    .on_update = app_update,
    .on_render = app_render,
    .on_event = app_event
)
