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
#include "gpu.texture.h"
#include "gpu.cmd.h"
#include "sprite.pass.h"
#include "font.atlas.h"
#include "texture.pool.h"
#include "tile.set.h"
#include "tile.map.h"
#include "ui.widget.panel.h"
#include "ui.widget.label.h"
#include "game.h"
#include "vfs.h"
#include "vfs.backend.os.h"
#include "async.io.h"
#include "allocator.heap.h"
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
#include "render.graph.h"
#include "render.target.h"
#include "render.camera.h"
#include "render.list.h"
#include "sim.ctx.h"

static SDL_Window* s_window;

static Mel_Tileset_Pool s_tileset_pool;
static Mel_Tilemap_Pool s_tilemap_pool;
static Mel_Font_Atlas_Pool s_font_pool;
static Mel_Font_Handle s_font_handle;
static Mel_EdRegistry s_ed_registry;
static Mel_GameEditor s_game_editor;
static Mel_Io s_io;
static Mel_Vfs s_vfs;
static Mel_Vfs_Backend* s_os_backend;
static Mel_Vfs_Backend* s_fonts_backend;
static Mel_Gpu_Texture s_test_texture;
static Mel_Game s_game;

static Mel_WPanel s_dialogue_box;
static Mel_WLabel s_dialogue_label;
static Mel_Render_Target s_swapchain_target;
static Mel_Render_Graph s_graph;
static Mel_Camera s_camera;
static Mel_Engine* s_engine;
static Mel_Sim_Ctx s_sim;
static u8 s_event_buf[4096];

static Mel_Render_List s_sprite_list;
static Mel_Render_List s_ui_list;

static Mel_EdEntities* s_ed_entities;
static Mel_EdTiles* s_ed_tiles;
static Mel_EdSpritesheet* s_ed_spritesheet;

static Mel_Alloc* s_alloc;

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

static void imgui_render_pass(Mel_Render_Pass_Ctx* ctx);

static void on_init(Mel_Engine* e)
{
    s_alloc = &e->allocator;
    Mel_Alloc* alloc = s_alloc;
    Mel_Gpu_Device* dev = &e->dev;

    mel_render_list_init(&s_sprite_list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    mel_render_list_init(&s_ui_list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    Mel_Texture_Pool* pool = e->texture_pool;

    mel_tileset_pool_init(&s_tileset_pool, alloc, pool, &s_vfs);
    mel_tilemap_pool_init(&s_tilemap_pool, alloc, &s_tileset_pool, &s_vfs);
    mel_font_atlas_pool_init(&s_font_pool, alloc, dev, &s_vfs, .texture_pool = pool);

    s_font_handle = mel_font_atlas_pool_load(&s_font_pool, .path = S8("sys/fonts/Monaco.ttf"), .size = 20.0f);

    mel_texture_load_and_bind(&s_test_texture, dev, &e->sprite_pass->pipeline, &s_vfs, alloc, S8("test.png"));

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
    mel_ed_tiles_set_pools(s_ed_tiles, &s_tileset_pool, &s_tilemap_pool, pool);

    s_ed_spritesheet = mel_alloc_type(alloc, Mel_EdSpritesheet);
    mel_ed_spritesheet_init(s_ed_spritesheet, alloc);
    s_ed_spritesheet->texture_pool = pool;

    mel_game_editor_init(&s_game_editor, &s_ed_registry, pool, &s_tileset_pool, &s_tilemap_pool, s_window);

    s_engine = e;

    mel_render_target_init_swapchain(&s_swapchain_target, &e->swapchain, &e->dev, S8("backbuffer"));

    s_camera = (Mel_Camera){
        .view = MEL_MAT4_IDENTITY,
        .projection = mel_mat4_ortho(0, (f32)e->swapchain.extent.width,
                                      0, (f32)e->swapchain.extent.height, -1, 1),
    };

    mel_render_graph_init(&s_graph, .dev = &e->dev, .alloc = mel_alloc_heap());

    u32 game_pass = mel_render_graph_add_pass(&s_graph, S8("game"),
        .fn = mel_sprite_pass_execute,
        .user = e->sprite_pass,
        .camera = &s_camera,
        .read_lists = MEL_LISTS(&s_sprite_list),
        .write_targets = MEL_WRITE_TARGETS(
            { .target = &s_swapchain_target, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .clear.color = { .r = 0.1f, .g = 0.1f, .b = 0.12f, .a = 1.0f } }));

    u32 ui_pass = mel_render_graph_add_pass(&s_graph, S8("ui"),
        .fn = mel_sprite_pass_execute,
        .user = e->sprite_pass,
        .camera = &s_camera,
        .read_lists = MEL_LISTS(&s_ui_list),
        .write_targets = MEL_WRITE_TARGETS(
            { .target = &s_swapchain_target, .load_op = VK_ATTACHMENT_LOAD_OP_LOAD }));

    u32 imgui_pass = mel_render_graph_add_pass(&s_graph, S8("imgui"),
        .fn = imgui_render_pass,
        .write_targets = MEL_WRITE_TARGETS(
            { .target = &s_swapchain_target, .load_op = VK_ATTACHMENT_LOAD_OP_LOAD }));

    mel_render_graph_pass_depends_on(&s_graph, ui_pass, game_pass);
    mel_render_graph_pass_depends_on(&s_graph, imgui_pass, ui_pass);
    mel_render_graph_compile(&s_graph);
    e->render_graph = &s_graph;

    SDL_Log("Game ready! WASD to move, E to interact with NPC, F1 for editor");
}

static void app_update(Mel_Sim_Ctx* sim, f32 dt, void* user);

static void app_init(Mel_App* app)
{
    const char* base = SDL_GetBasePath();
    char assets_path[1024];
    snprintf(assets_path, sizeof(assets_path), "%sassets", base);

    Mel_Io_Desc io_desc = { .allocator = mel_alloc_heap(), .sq_capacity = 256, .cq_capacity = 256, .worker_count = 0 };
    if (!mel_io_init(&s_io, &io_desc))
    {
        SDL_Log("Failed to init IO system!");
        return;
    }

    Mel_Vfs_Desc vfs_desc = { .allocator = mel_alloc_heap(), .io = &s_io };
    if (!mel_vfs_init(&s_vfs, &vfs_desc))
    {
        SDL_Log("Failed to init VFS!");
        mel_io_shutdown(&s_io);
        return;
    }

    s_os_backend = mel_vfs_backend_os_create(mel_alloc_heap(), str8_from_cstr(assets_path));
    mel_vfs_mount(&s_vfs, S8("."), s_os_backend, 128, true);

    s_fonts_backend = mel_vfs_backend_os_create(mel_alloc_heap(), S8("/System/Library/Fonts"));
    mel_vfs_mount(&s_vfs, S8("sys/fonts"), s_fonts_backend, 0, false);

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
        .enable_imgui = true);

    on_init(&app->engine);

    mel_sim_init(&s_sim, .event_buffer = s_event_buf, .event_buffer_size = sizeof(s_event_buf));
    mel_sim_add_variable(&s_sim, app_update);
    mel_engine_register_sim(&app->engine, &s_sim);
}

static void app_shutdown(Mel_App* app)
{
    mel_engine_unregister_sim(&app->engine, &s_sim);
    mel_sim_shutdown(&s_sim);

    Mel_Gpu_Device* dev = &app->engine.dev;

    mel_gpu_device_wait_idle(dev);

    mel_render_graph_shutdown(&s_graph);
    mel_render_target_shutdown(&s_swapchain_target);

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
    mel_font_atlas_pool_shutdown(&s_font_pool);
    mel_tilemap_pool_shutdown(&s_tilemap_pool);
    mel_tileset_pool_shutdown(&s_tileset_pool);
    mel_gpu_texture_shutdown(&s_test_texture, dev);

    mel_render_list_shutdown(&s_sprite_list);
    mel_render_list_shutdown(&s_ui_list);

    mel_vfs_shutdown(&s_vfs);
    mel_io_shutdown(&s_io);
    mel_vfs_backend_os_destroy(s_os_backend);
    mel_vfs_backend_os_destroy(s_fonts_backend);
    SDL_Log("Game shutdown");
}

static void app_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(user);
    mel_game_update(&s_game, dt);
    mel_widget_set_visible(&s_dialogue_box.base, s_game.dialogue_open);

    mel_render_list_clear(&s_sprite_list);
    mel_render_list_clear(&s_ui_list);

    mel_game_draw(&s_game, &s_sprite_list);

    if (s_dialogue_box.base.visible)
    {
        mel_widget_draw(&s_dialogue_box.base, &s_ui_list);
    }
}

static void imgui_render_pass(Mel_Render_Pass_Ctx* ctx)
{
    TracyCZoneN(zone, "imgui_render_pass", true);

    if (s_game_editor.visible)
        mel_game_editor_draw(&s_game_editor, s_engine->frame_dt);

    igRender();
    ImDrawData* draw_data = igGetDrawData();
    if (draw_data && draw_data->CmdListsCount > 0)
        ImGui_ImplVulkan_RenderDrawData(draw_data, ctx->cmd.cmd, VK_NULL_HANDLE);

    TracyCZoneEnd(zone);
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
    .on_event = app_event
)
