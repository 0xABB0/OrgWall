#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define CIMGUI_USE_SDL3
#define CIMGUI_USE_VULKAN
#include <volk.h>
#include <cimgui/cimgui.h>
#include <cimgui/cimgui_impl.h>

#include <string.h>

#include "core.app.h"
#include "core.engine.h"
#include "window.h"
#include "swapchain.h"
#include "gpu.swapchain.h"
#include "allocator.heap.h"
#include "font.atlas.h"
#include "render.frame_plan.h"
#include "render.stage.2d.h"
#include "render.list.h"
#include "sprite.pass.h"
#include "math.mat4.h"
#include "math.vec4.h"
#include "sim.ctx.h"
#include "async.io.h"
#include "async.task.h"
#include "async.job.h"
#include "mugen.roster.h"
#include "string.path.h"
#include "test.harness.h"

#include "street_carlos_ctx.h"
#include "street_carlos_flow.h"
#include "street_carlos_title_stage.h"
#include "street_carlos_main_menu_stage.h"
#include "street_carlos_char_select_stage.h"
#include "street_carlos_stage_select_stage.h"
#include "street_carlos_loading_stage.h"
#include "street_carlos_fight_stage.h"
#include "street_carlos_pause_stage.h"
#include "street_carlos_console_stage.h"

static Street_Carlos_Ctx s_app;
static Street_Carlos_Title_Stage s_title_stage;
static Street_Carlos_Main_Menu_Stage s_main_menu_stage;
static Street_Carlos_Char_Select_Stage s_char_select_stage;
static Street_Carlos_Stage_Select_Stage s_stage_select_stage;
static Street_Carlos_Loading_Stage s_loading_stage;
static Street_Carlos_Fight_Stage s_fight_stage;
static Street_Carlos_Pause_Stage s_pause_stage;
static Street_Carlos_Console_Stage s_console_stage;

static void street_carlos_imgui(void* user)
{
    MEL_UNUSED(user);
    street_carlos_fight_stage_show_imgui(&s_fight_stage);
}

static void shell_clear_stage_choices(void)
{
    for (u32 i = 0; i < s_app.stage_choice_count; i++)
    {
        if (s_app.stage_choices[i].path.data)
            mel_dealloc(mel_alloc_heap(), s_app.stage_choices[i].path.data);
        if (s_app.stage_choices[i].label.data)
            mel_dealloc(mel_alloc_heap(), s_app.stage_choices[i].label.data);
        s_app.stage_choices[i] = (Street_Carlos_Stage_Choice){0};
    }
    s_app.stage_choice_count = 0;
}

static bool stage_catalog_enum_cb(str8 virtual_path, const Mel_Vfs_Stat* stat, void* user)
{
    MEL_UNUSED(user);
    if (stat->flags & MEL_VFS_STAT_IS_DIR)
        return true;

    str8 ext = mel_path_extension(virtual_path);
    if (!str8_ieq_cstr(ext, "def"))
        return true;

    if (s_app.stage_choice_count >= STREET_CARLOS_MAX_STAGE_CHOICES)
        return false;

    str8 filename = mel_path_filename(virtual_path);
    str8 label = filename;
    if (ext.len > 0 && label.len > ext.len + 1)
        label.len -= ext.len + 1;

    s_app.stage_choices[s_app.stage_choice_count++] = (Street_Carlos_Stage_Choice){
        .path = str8_dup(virtual_path, mel_alloc_heap()),
        .label = str8_dup(label, mel_alloc_heap()),
    };
    return true;
}

static void load_stage_choices(void)
{
    shell_clear_stage_choices();
    mel_vfs_enumerate(&s_app.vfs, S8("/stages"), stage_catalog_enum_cb, NULL, .recursive = true);

    if (s_app.stage_choice_count == 0)
    {
        s_app.stage_choices[0] = (Street_Carlos_Stage_Choice){
            .path = str8_dup(S8("/stages/kfm.def"), mel_alloc_heap()),
            .label = str8_dup(S8("kfm"), mel_alloc_heap()),
        };
        s_app.stage_choice_count = 1;
    }
}

static void produce_world_list(Mel_Render_List* list, void* user)
{
    Street_Carlos_Ctx* ctx = user;
    street_carlos_title_stage_draw_world(&s_title_stage, ctx, list);
    street_carlos_main_menu_stage_draw_world(&s_main_menu_stage, ctx, list);
    street_carlos_char_select_stage_draw_world(&s_char_select_stage, ctx, list);
    street_carlos_stage_select_stage_draw_world(&s_stage_select_stage, ctx, list);
    street_carlos_loading_stage_draw_world(&s_loading_stage, ctx, list);
    street_carlos_fight_stage_draw_world(&s_fight_stage, ctx, list);
}

static void produce_hud_list(Mel_Render_List* list, void* user)
{
    street_carlos_fight_stage_draw_hud(&s_fight_stage, user, list);
}

static void produce_debug_list(Mel_Render_List* list, void* user)
{
    street_carlos_fight_stage_draw_debug(&s_fight_stage, user, list);
}

static void task_sim_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    Street_Carlos_Ctx* ctx = user;
    MEL_UNUSED(sim);
    MEL_UNUSED(dt);

    mel_task_tick(ctx->task_ctx);
    street_carlos_fight_stage_tick(&s_fight_stage, ctx);
}

static void load_roster_sync(void)
{
    Mel_Task_Handle task = mugen_roster_load(&s_app.roster, .folder_path = S8("/chars/"));
    mel_task_wait(s_app.task_ctx, task);
    if (mel_task_status(s_app.task_ctx, task) != MEL_TASK_STATUS_DONE)
        SDL_Log("Roster loading failed during init");
    mel_task_release(s_app.task_ctx, task);
}

static void init_render(void)
{
    mel_render_list_init(&s_app.world_list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .mode = MEL_RENDER_LIST_EPHEMERAL,
        .alloc = mel_alloc_heap());
    mel_render_list_add_producer(&s_app.world_list, produce_world_list, &s_app);

    s_app.game_camera = (Mel_Camera){
        .view = MEL_MAT4_IDENTITY,
        .projection = mel_mat4_ortho(0, (f32)GAME_W, 0, (f32)GAME_H, -1, 1),
    };

    mel_render_list_init(&s_app.hud_list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .mode = MEL_RENDER_LIST_EPHEMERAL,
        .alloc = mel_alloc_heap());
    mel_render_list_add_producer(&s_app.hud_list, produce_hud_list, &s_app);

    mel_render_list_init(&s_app.debug_list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .mode = MEL_RENDER_LIST_EPHEMERAL,
        .alloc = mel_alloc_heap());
    mel_render_list_add_producer(&s_app.debug_list, produce_debug_list, &s_app);

    bool ok = mel_render_stage_2d_init(&s_app.render_stage,
        .name = S8("street_carlos"),
        .swapchain = s_app.swapchain_handle,
        .world_camera = &s_app.game_camera,
        .hud_camera = &s_app.game_camera,
        .debug_camera = &s_app.game_camera,
        .ui_camera = &s_app.game_camera,
        .clear_color_enabled = true,
        .clear_color = mel_vec4(0.0f, 0.0f, 0.0f, 1.0f),
        .design_width = GAME_W,
        .design_height = GAME_H,
        .enable_imgui = true,
        .imgui_fn = street_carlos_imgui,
        .install_as_current_graph = true,
        .dev = mel_gpu_dev(),
        .sprite_pass = mel_sprite_pass(),
        .text_pass = mel_text_pass(),
        .alloc = mel_alloc_heap());
    assert(ok);

    mel_render_stage_2d_attach_sprite_list_to_layer(&s_app.render_stage, MEL_RENDER_STAGE_2D_LAYER_WORLD, &s_app.world_list);
    mel_render_stage_2d_attach_sprite_list_to_layer(&s_app.render_stage, MEL_RENDER_STAGE_2D_LAYER_HUD, &s_app.hud_list);
    mel_render_stage_2d_attach_sprite_list_to_layer(&s_app.render_stage, MEL_RENDER_STAGE_2D_LAYER_DEBUG, &s_app.debug_list);
    ok = mel_render_stage_2d_rebuild(&s_app.render_stage);
    assert(ok);
}

static void on_init(void)
{
    Mel_Window_Handle window_handle = s_app.window_handle;
    Mel_Swapchain_Handle swapchain_handle = s_app.swapchain_handle;

    memset(&s_app, 0, sizeof(s_app));
    s_app.window_handle = window_handle;
    s_app.swapchain_handle = swapchain_handle;

    s_app.title_stage = &s_title_stage;
    s_app.main_menu_stage = &s_main_menu_stage;
    s_app.char_select_stage = &s_char_select_stage;
    s_app.stage_select_stage = &s_stage_select_stage;
    s_app.loading_stage = &s_loading_stage;
    s_app.fight_stage = &s_fight_stage;
    s_app.pause_stage = &s_pause_stage;
    s_app.console_stage = &s_console_stage;

    init_render();

    mel_io_init(&s_app.io, &(Mel_Io_Desc){ .allocator = mel_alloc_heap(), .worker_count = 0 });
    mel_vfs_init(&s_app.vfs, &(Mel_Vfs_Desc){ .allocator = mel_alloc_heap(), .io = &s_app.io });
    mel_vfs_mount_native(&s_app.vfs, S8("/"), S8("demos/street-carlos"), 0, false);
    mel_vfs_mount_native(&s_app.vfs, S8("/fonts"), S8("/System/Library/Fonts"), 0, false);
    mel_vfs_mount_native(&s_app.vfs, S8("/stages"), S8("demos/street-carlos/stages"), 0, false);
    mel_vfs_mount_native(&s_app.vfs, S8("/data"), S8("demos/street-carlos/data"), 0, false);

    mel_font_atlas_pool_init(&s_app.font_pool, mel_alloc_heap(), mel_gpu_dev(), &s_app.vfs, .texture_pool = mel_texture_pool());
    s_app.title_font = mel_font_atlas_pool_load(&s_app.font_pool, .path = S8("/fonts/Monaco.ttf"), .size = 20.0f);
    s_app.ui_font = mel_font_atlas_pool_load(&s_app.font_pool, .path = S8("/fonts/Monaco.ttf"), .size = 14.0f);
    s_app.body_font = mel_font_atlas_pool_load(&s_app.font_pool, .path = S8("/fonts/Monaco.ttf"), .size = 10.0f);

    s_app.job_ctx = mel_job_create_context(mel_alloc_heap());
    Mel_Task_Ctx_Desc task_desc = {
        .alloc = mel_alloc_heap(),
        .io = &s_app.io,
        .vfs = &s_app.vfs,
        .jobs = s_app.job_ctx,
    };
    s_app.task_ctx = mel_task_ctx_create(&task_desc);

    mugen_roster_init(&s_app.roster,
        .vfs = &s_app.vfs,
        .task_ctx = s_app.task_ctx,
        .stcommon_path = S8("/chars/common1.cns"),
        .alloc = mel_alloc_heap());
    load_roster_sync();
    load_stage_choices();

    mel_sim_init(&s_app.task_sim,
        .event_buffer = s_app.task_event_buf,
        .event_buffer_size = sizeof(s_app.task_event_buf),
        .alloc = mel_alloc_heap());
    mel_sim_add_variable(&s_app.task_sim, task_sim_update, .user = &s_app);
    mel_register_sim(&s_app.task_sim);

    mel_progress_init(&s_app.load_progress, mel_alloc_heap());

    street_carlos_fight_stage_init(&s_fight_stage, &s_app);
    street_carlos_loading_stage_init(&s_loading_stage, &s_app);
    street_carlos_pause_stage_init(&s_pause_stage, &s_app);
    street_carlos_console_stage_init(&s_console_stage, &s_app);
    street_carlos_title_stage_init(&s_title_stage, &s_app);
    street_carlos_main_menu_stage_init(&s_main_menu_stage, &s_app);
    street_carlos_char_select_stage_init(&s_char_select_stage, &s_app);
    street_carlos_stage_select_stage_init(&s_stage_select_stage, &s_app);
    assert(mel_render_stage_2d_rebuild(&s_app.render_stage));

    street_carlos_show_title(&s_app);
    mel_stage_tick();
}

static void app_init(Mel_App* app)
{
    if (app->argc > 1 && strcmp(app->argv[1], "--test") == 0)
    {
        int result = mel_test_main(app->argc, app->argv);
        app->should_quit = true;
        exit(result);
    }

    mel_init(.app_name = S8("Street Carlos"), .enable_validation = true);
    s_app.window_handle = mel_window_create(S8("Street Carlos"), .width = GAME_W * 3, .height = GAME_H * 3);
    s_app.swapchain_handle = mel_gpu_swapchain_create_for_window(mel_gpu_dev(), s_app.window_handle);
    mel_imgui_init(s_app.window_handle, &mel_swapchain_registry_get(s_app.swapchain_handle)->swapchain);

    on_init();
}

static void app_shutdown(Mel_App* app)
{
    MEL_UNUSED(app);

    mel_stage_detach(&s_title_stage.stage);
    mel_stage_detach(&s_main_menu_stage.stage);
    mel_stage_detach(&s_char_select_stage.stage);
    mel_stage_detach(&s_stage_select_stage.stage);
    mel_stage_detach(&s_loading_stage.base.stage);
    mel_stage_detach(&s_fight_stage.stage);
    mel_stage_detach(&s_pause_stage.stage);
    mel_stage_detach(&s_console_stage.stage);
    mel_stage_tick();

    street_carlos_stage_select_stage_shutdown(&s_stage_select_stage);
    street_carlos_char_select_stage_shutdown(&s_char_select_stage, &s_app);
    street_carlos_main_menu_stage_shutdown(&s_main_menu_stage);
    street_carlos_title_stage_shutdown(&s_title_stage);
    street_carlos_console_stage_shutdown(&s_console_stage);
    street_carlos_pause_stage_shutdown(&s_pause_stage);
    street_carlos_loading_stage_shutdown(&s_loading_stage);
    street_carlos_fight_stage_shutdown(&s_fight_stage, &s_app);
    mel_progress_destroy(&s_app.load_progress);
    shell_clear_stage_choices();

    mel_unregister_sim(&s_app.task_sim);
    mel_sim_shutdown(&s_app.task_sim);
    mugen_roster_shutdown(&s_app.roster);

    mel_render_stage_2d_shutdown(&s_app.render_stage);
    mel_render_list_shutdown(&s_app.debug_list);
    mel_render_list_shutdown(&s_app.hud_list);
    mel_render_list_shutdown(&s_app.world_list);

    mel_font_atlas_pool_shutdown(&s_app.font_pool);
    mel_vfs_unmount(&s_app.vfs, S8("/"));
    mel_vfs_unmount(&s_app.vfs, S8("/data"));
    mel_vfs_unmount(&s_app.vfs, S8("/stages"));
    mel_vfs_unmount(&s_app.vfs, S8("/fonts"));
    mel_task_ctx_destroy(s_app.task_ctx);
    mel_job_destroy_context(s_app.job_ctx, mel_alloc_heap());
    mel_vfs_shutdown(&s_app.vfs);
    mel_io_shutdown(&s_app.io);
}

static void app_event(Mel_App* app, SDL_Event* event)
{
    if (street_carlos_console_stage_handle_event(&s_console_stage, &s_app, event)) return;
    if (street_carlos_pause_stage_handle_event(&s_pause_stage, &s_app, event)) return;
    if (street_carlos_fight_stage_handle_event(&s_fight_stage, &s_app, event)) return;
    if (street_carlos_loading_stage_handle_event(&s_loading_stage, &s_app, event)) return;
    if (street_carlos_stage_select_stage_handle_event(&s_stage_select_stage, &s_app, event)) return;
    if (street_carlos_char_select_stage_handle_event(&s_char_select_stage, &s_app, event)) return;
    if (street_carlos_main_menu_stage_handle_event(&s_main_menu_stage, &s_app, event)) return;
    if (street_carlos_title_stage_handle_event(&s_title_stage, &s_app, event)) return;

    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE)
        app->should_quit = true;
}

MEL_APP(
    .on_init = app_init,
    .on_shutdown = app_shutdown,
    .on_event = app_event
)
