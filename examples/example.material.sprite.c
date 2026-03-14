#include <SDL3/SDL.h>

#include <cimgui/cimgui.h>

#include "core.app.h"
#include "core.engine.h"
#include "window.h"
#include "swapchain.h"
#include "gpu.swapchain.h"
// ASYNC_V2: VFS removed
// #include "vfs.h"
#include "string.str8.h"
#include "render.stage.2d.h"
#include "render.frame_plan.h"
#include "render.material.h"
#include "render.list.h"
#include "render.camera.h"
#include "render.view.h"
#include "sprite.pass.h"
#include "text.pass.h"
#include "text.draw.h"
#include "font.atlas.h"
#include "allocator.heap.h"
#include "math.mat4.h"
#include "math.vec2.h"
#include "math.vec4.h"
#include "sim.ctx.h"

#define WIN_W 1280
#define WIN_H 800

typedef enum {
    MATERIAL_POLICY_DEFAULT = 0,
    MATERIAL_POLICY_STOCK,
    MATERIAL_POLICY_CUSTOM,
} Material_Policy_Mode;

static const str8 s_custom_unlit_backend = S8("game.sprite.unlit_debug");
static const str8 s_custom_badge_backend = S8("game.sprite.badge");

static Mel_Window_Handle s_window_handle;
static Mel_Swapchain_Handle s_swapchain_handle;
static Mel_Render_Stage_2D s_stage;
static Mel_Render_List s_world_list;
static Mel_Render_List s_hud_text;
static Mel_Camera s_world_camera;
static Mel_Camera s_hud_camera;
static Mel_Sim_Ctx s_sim;
static u8 s_event_buf[4096];
static Mel_Font_Handle s_font;

static Mel_Material_Template_Handle s_card_template;
static Mel_Material_Template_Handle s_badge_template;
static Mel_Material_Instance_Handle s_player_card;
static Mel_Material_Instance_Handle s_enemy_card;
static Mel_Material_Instance_Handle s_badge;

static Material_Policy_Mode s_policy_mode = MATERIAL_POLICY_DEFAULT;
static Material_Policy_Mode s_requested_policy_mode = MATERIAL_POLICY_DEFAULT;
static float s_player_color[4] = { 0.36f, 0.79f, 0.98f, 1.0f };
static float s_enemy_color[4] = { 0.96f, 0.44f, 0.52f, 1.0f };
static float s_badge_color[4] = { 0.99f, 0.84f, 0.31f, 1.0f };
static float s_clear_color[4] = { 0.08f, 0.09f, 0.12f, 1.0f };
static f32 s_time;

static str8 material_sprite_policy_label(Material_Policy_Mode mode)
{
    switch (mode)
    {
        case MATERIAL_POLICY_DEFAULT: return S8("default");
        case MATERIAL_POLICY_STOCK: return S8("stock preferred");
        case MATERIAL_POLICY_CUSTOM: return S8("custom preferred");
        default: return S8("unknown");
    }
}

static Mel_Material_Check_Result material_sprite_support_always(Mel_Frame_Plan_Material_Ctx* ctx,
    Mel_Material_Template_Handle material_template)
{
    MEL_UNUSED(ctx);
    MEL_UNUSED(material_template);
    return (Mel_Material_Check_Result){
        .ok = true,
        .kind = MEL_MATERIAL_CHECK_OK,
        .reason = S8("supported by current device"),
    };
}

static Mel_Material_Check_Result material_sprite_match_unlit_debug(Mel_Frame_Plan_Material_Ctx* ctx,
    Mel_Material_Template_Handle material_template)
{
    if (!str8_ieq(ctx->technique_name, S8("sprite")))
    {
        return (Mel_Material_Check_Result){
            .ok = false,
            .kind = MEL_MATERIAL_CHECK_TECHNIQUE_MISMATCH,
            .reason = S8("custom unlit backend requires sprite technique"),
        };
    }

    if (!str8_ieq(mel_material_template_profile(material_template), S8("sprite.unlit")))
    {
        return (Mel_Material_Check_Result){
            .ok = false,
            .kind = MEL_MATERIAL_CHECK_PROFILE_MISMATCH,
            .reason = S8("custom unlit backend expects sprite.unlit"),
        };
    }

    return (Mel_Material_Check_Result){
        .ok = true,
        .kind = MEL_MATERIAL_CHECK_OK,
        .reason = S8("custom unlit backend matches sprite.unlit materials"),
    };
}

static Mel_Material_Check_Result material_sprite_match_badge(Mel_Frame_Plan_Material_Ctx* ctx,
    Mel_Material_Template_Handle material_template)
{
    if (!str8_ieq(ctx->technique_name, S8("sprite")))
    {
        return (Mel_Material_Check_Result){
            .ok = false,
            .kind = MEL_MATERIAL_CHECK_TECHNIQUE_MISMATCH,
            .reason = S8("badge backend requires sprite technique"),
        };
    }

    if (!str8_ieq(mel_material_template_profile(material_template), S8("sprite.badge")))
    {
        return (Mel_Material_Check_Result){
            .ok = false,
            .kind = MEL_MATERIAL_CHECK_PROFILE_MISMATCH,
            .reason = S8("badge backend expects sprite.badge"),
        };
    }

    return (Mel_Material_Check_Result){
        .ok = true,
        .kind = MEL_MATERIAL_CHECK_OK,
        .reason = S8("badge backend matches sprite.badge materials"),
    };
}

static Mel_Material_Policy_Result material_sprite_policy(Mel_Frame_Plan_Material_Ctx* ctx,
    const Mel_Material_Backend_Desc* backend, Mel_Material_Template_Handle material_template, void* user)
{
    MEL_UNUSED(ctx);
    MEL_UNUSED(material_template);
    Material_Policy_Mode mode = *(Material_Policy_Mode*)user;
    if (mode == MATERIAL_POLICY_DEFAULT)
    {
        return (Mel_Material_Policy_Result){
            .allow = true,
            .priority_bias = 0,
            .kind = MEL_MATERIAL_CHECK_OK,
            .reason = S8(""),
        };
    }

    if (mode == MATERIAL_POLICY_STOCK)
    {
        if (str8_ieq(backend->name, S8("sprite.unlit.sprite")))
        {
            return (Mel_Material_Policy_Result){
                .allow = true,
                .priority_bias = 250,
                .kind = MEL_MATERIAL_CHECK_OK,
                .reason = S8("stock policy prefers engine sprite backend"),
            };
        }
        if (str8_ieq(backend->name, s_custom_unlit_backend))
        {
            return (Mel_Material_Policy_Result){
                .allow = true,
                .priority_bias = -150,
                .kind = MEL_MATERIAL_CHECK_OK,
                .reason = S8("stock policy leaves custom unlit backend as fallback"),
            };
        }
        return (Mel_Material_Policy_Result){
            .allow = true,
            .priority_bias = 0,
            .kind = MEL_MATERIAL_CHECK_OK,
            .reason = S8(""),
        };
    }

    if (str8_ieq(backend->name, s_custom_unlit_backend))
    {
        return (Mel_Material_Policy_Result){
            .allow = true,
            .priority_bias = 300,
            .kind = MEL_MATERIAL_CHECK_OK,
            .reason = S8("custom policy prefers the game unlit backend"),
        };
    }
    if (str8_ieq(backend->name, S8("sprite.unlit.sprite")))
    {
        return (Mel_Material_Policy_Result){
            .allow = true,
            .priority_bias = -120,
            .kind = MEL_MATERIAL_CHECK_OK,
            .reason = S8("custom policy keeps the stock backend as fallback"),
        };
    }

    return (Mel_Material_Policy_Result){
        .allow = true,
        .priority_bias = 0,
        .kind = MEL_MATERIAL_CHECK_OK,
        .reason = S8(""),
    };
}

static void material_sprite_register_backends(void)
{
    Mel_Material_Family_Handle sprite = mel_material_family_find(S8("sprite"));
    mel_material_backend_register(&(Mel_Material_Backend_Desc){
        .name = s_custom_unlit_backend,
        .family = sprite,
        .profile = S8("sprite.unlit"),
        .technique_family = MEL_TECHNIQUE_SPRITE,
        .technique_name = S8("sprite"),
        .priority = 50,
        .supports = material_sprite_support_always,
        .matches = material_sprite_match_unlit_debug,
    });
    mel_material_backend_register(&(Mel_Material_Backend_Desc){
        .name = s_custom_badge_backend,
        .family = sprite,
        .profile = S8("sprite.badge"),
        .technique_family = MEL_TECHNIQUE_SPRITE,
        .technique_name = S8("sprite"),
        .priority = 120,
        .supports = material_sprite_support_always,
        .matches = material_sprite_match_badge,
    });
}

static void material_sprite_unregister_backends(void)
{
    Mel_Material_Family_Handle sprite = mel_material_family_find(S8("sprite"));
    mel_material_backend_unregister(sprite, s_custom_unlit_backend);
    mel_material_backend_unregister(sprite, s_custom_badge_backend);
}

static void material_sprite_apply_policy(Material_Policy_Mode mode)
{
    Mel_Material_Family_Handle sprite = mel_material_family_find(S8("sprite"));
    s_policy_mode = mode;
    s_requested_policy_mode = mode;
    if (mode == MATERIAL_POLICY_DEFAULT)
    {
        mel_material_clear_family_policy(sprite);
        return;
    }

    mel_material_set_family_policy(&(Mel_Material_Family_Policy){
        .family = sprite,
        .fn = material_sprite_policy,
        .user = &s_policy_mode,
    });
}

static void material_sprite_request_policy(Material_Policy_Mode mode)
{
    s_requested_policy_mode = mode;
}

static void material_sprite_sync_viewport(void)
{
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;
    Mel_View_Handle world_view = mel_render_stage_2d_view(&s_stage, MEL_RENDER_STAGE_2D_LAYER_WORLD);
    i32 w = 0;
    i32 h = 0;
    mel_window_size_pixels(s_window_handle, &w, &h);
    if (w <= 0 || h <= 0)
        return;

    s_world_camera.view = MEL_MAT4_IDENTITY;
    s_world_camera.projection = mel_mat4_ortho(0.0f, (f32)w, (f32)h, 0.0f, -1.0f, 1.0f);
    s_hud_camera = s_world_camera;
    mel_view_set_clear_color_enabled(world_view, true);
    mel_view_set_clear_color(world_view,
        mel_vec4(s_clear_color[0], s_clear_color[1], s_clear_color[2], s_clear_color[3]));

    if (sc->extent.width != (u32)w || sc->extent.height != (u32)h)
        mel_swapchain_resize(sc, mel_gpu_dev(), (u32)w, (u32)h);

    bool ok = mel_render_stage_2d_refresh(&s_stage);
    assert(ok);
}

static str8 material_sprite_resolved_backend(Mel_Material_Instance_Handle material)
{
    Mel_Frame_Plan_Handle plan = mel_render_stage_2d_plan(&s_stage);
    Mel_View_Handle world_view = mel_render_stage_2d_view(&s_stage, MEL_RENDER_STAGE_2D_LAYER_WORLD);
    u32 count = mel_frame_plan_resolved_material_count(plan);
    for (u32 i = 0; i < count; i++)
    {
        Mel_Frame_Plan_Resolved_Material resolved = {0};
        if (!mel_frame_plan_resolved_material_at(plan, i, &resolved))
            continue;
        if (resolved.view.handle.index != world_view.handle.index ||
            resolved.view.handle.generation != world_view.handle.generation)
            continue;
        if (resolved.material_instance.handle.index != material.handle.index ||
            resolved.material_instance.handle.generation != material.handle.generation)
            continue;
        return resolved.backend_name;
    }

    return S8("none");
}

static u32 material_sprite_diagnostic_lines(char lines[][160], u32 max_lines)
{
    for (u32 i = 0; i < max_lines; i++)
        lines[i][0] = '\0';

    Mel_Frame_Plan_Handle plan = mel_render_stage_2d_plan(&s_stage);
    Mel_View_Handle world_view = mel_render_stage_2d_view(&s_stage, MEL_RENDER_STAGE_2D_LAYER_WORLD);
    u32 count = mel_frame_plan_material_diagnostic_count(plan);
    u32 found = 0;

    for (u32 i = 0; i < count && found < max_lines; i++)
    {
        Mel_Frame_Plan_Material_Diagnostic diag = {0};
        if (!mel_frame_plan_material_diagnostic_at(plan, i, &diag))
            continue;
        if (diag.view.handle.index != world_view.handle.index ||
            diag.view.handle.generation != world_view.handle.generation)
            continue;

        SDL_snprintf(lines[found], 160, "%s%.*s -> %.*s: %.*s",
            diag.selected ? "* " : "  ",
            (int)mel_material_template_name(diag.material_template).len,
            mel_material_template_name(diag.material_template).data,
            (int)diag.backend_name.len, diag.backend_name.data,
            (int)diag.reason.len, diag.reason.data);
        found++;
    }

    return found;
}

static void material_sprite_draw_card(Mel_Render_List* list, Mel_Material_Instance_Handle material,
    f32 x, f32 y, f32 w, f32 h, Mel_Vec4 accent)
{
    mel_draw_sprite(list,
        .pos = mel_vec2(x, y),
        .size = mel_vec2(w, h),
        .color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
        .material = material);
    mel_draw_sprite(list,
        .pos = mel_vec2(x + 14.0f, y + h - 20.0f),
        .size = mel_vec2(w - 28.0f, 6.0f),
        .color = accent,
        .material = material);
}

static void material_sprite_imgui(void* user)
{
    MEL_UNUSED(user);

    str8 player_backend = material_sprite_resolved_backend(s_player_card);
    str8 badge_backend = material_sprite_resolved_backend(s_badge);
    char diag_lines[6][160];
    u32 diag_count = material_sprite_diagnostic_lines(diag_lines, SDL_arraysize(diag_lines));
    int policy_mode = s_policy_mode;

    igSetNextWindowPos((ImVec2){ 860.0f, 24.0f }, ImGuiCond_Once, (ImVec2){0, 0});
    igSetNextWindowSize((ImVec2){ 380.0f, 420.0f }, ImGuiCond_Once);
    if (igBegin("Sprite Materials", nullptr, 0))
    {
        igTextWrapped("This example keeps the sprite technique fixed and varies material templates, instances, and backend selection.");
        igSeparator();
        igText("Resolved card backend: %.*s", (int)player_backend.len, player_backend.data);
        igText("Resolved badge backend: %.*s", (int)badge_backend.len, badge_backend.data);
        igSeparator();
        if (igRadioButton_IntPtr("Policy: Default", &policy_mode, MATERIAL_POLICY_DEFAULT))
            material_sprite_request_policy(MATERIAL_POLICY_DEFAULT);
        if (igRadioButton_IntPtr("Policy: Stock Preferred", &policy_mode, MATERIAL_POLICY_STOCK))
            material_sprite_request_policy(MATERIAL_POLICY_STOCK);
        if (igRadioButton_IntPtr("Policy: Custom Preferred", &policy_mode, MATERIAL_POLICY_CUSTOM))
            material_sprite_request_policy(MATERIAL_POLICY_CUSTOM);
        igSeparator();
        igColorEdit4("Player Card", s_player_color, 0);
        igColorEdit4("Enemy Card", s_enemy_color, 0);
        igColorEdit4("Badge", s_badge_color, 0);
        igColorEdit4("Background", s_clear_color, 0);
        igSeparator();
        igText("Resolution trace:");
        for (u32 i = 0; i < diag_count; i++)
            igTextWrapped("%s", diag_lines[i]);
    }
    igEnd();
}

static void material_sprite_extract(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(dt);
    MEL_UNUSED(user);

    mel_material_instance_set_base_color(s_player_card,
        mel_vec4(s_player_color[0], s_player_color[1], s_player_color[2], s_player_color[3]));
    mel_material_instance_set_base_color(s_enemy_card,
        mel_vec4(s_enemy_color[0], s_enemy_color[1], s_enemy_color[2], s_enemy_color[3]));
    mel_material_instance_set_base_color(s_badge,
        mel_vec4(s_badge_color[0], s_badge_color[1], s_badge_color[2], s_badge_color[3]));

    mel_render_list_clear(&s_world_list);
    mel_render_list_clear(&s_hud_text);

    f32 pulse = 8.0f * sinf(s_time * 1.4f);
    material_sprite_draw_card(&s_world_list, s_player_card, 80.0f, 420.0f, 280.0f, 220.0f + pulse,
        mel_vec4(0.14f, 0.18f, 0.24f, 1.0f));
    material_sprite_draw_card(&s_world_list, s_enemy_card, 400.0f, 420.0f, 280.0f, 220.0f - pulse,
        mel_vec4(0.28f, 0.16f, 0.18f, 1.0f));
    mel_draw_sprite(&s_world_list,
        .pos = mel_vec2(160.0f, 240.0f),
        .size = mel_vec2(440.0f, 110.0f),
        .color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
        .material = s_badge);

    str8 player_backend = material_sprite_resolved_backend(s_player_card);
    str8 badge_backend = material_sprite_resolved_backend(s_badge);
    str8 policy = material_sprite_policy_label(s_policy_mode);
    char line[160];
    char line2[160];
    char diag_lines[6][160];
    u32 diag_count = material_sprite_diagnostic_lines(diag_lines, SDL_arraysize(diag_lines));

    SDL_snprintf(line, sizeof(line), "Policy: %.*s  Card backend: %.*s",
        (int)policy.len, policy.data,
        (int)player_backend.len, player_backend.data);
    SDL_snprintf(line2, sizeof(line2), "Badge backend: %.*s",
        (int)badge_backend.len, badge_backend.data);

    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, S8("Sprite Material Playground"),
        .x = 72.0f, .y = 104.0f,
        .style = { .color = mel_vec4(0.95f, 0.96f, 0.98f, 1.0f) });
    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text,
        S8("Instance colors are the visible parameter path in this slice. Backend selection is shown explicitly in diagnostics."),
        .x = 72.0f, .y = 72.0f,
        .style = { .color = mel_vec4(0.74f, 0.78f, 0.84f, 1.0f) });
    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, str8_from_cstr(line),
        .x = 72.0f, .y = 40.0f,
        .style = { .color = mel_vec4(0.80f, 0.88f, 0.98f, 1.0f) });
    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, str8_from_cstr(line2),
        .x = 72.0f, .y = 12.0f,
        .style = { .color = mel_vec4(0.94f, 0.83f, 0.64f, 1.0f) });

    for (u32 i = 0; i < diag_count; i++)
    {
        mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, str8_from_cstr(diag_lines[i]),
            .x = 72.0f,
            .y = 694.0f - (f32)(i * 28),
            .style = { .color = i == 0
                ? mel_vec4(0.86f, 0.88f, 0.92f, 1.0f)
                : mel_vec4(0.68f, 0.72f, 0.78f, 1.0f) });
    }

    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, S8("PLAYER MATERIAL"),
        .x = 116.0f, .y = 586.0f,
        .style = { .color = mel_vec4(0.08f, 0.11f, 0.14f, 1.0f) });
    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, S8("ENEMY MATERIAL"),
        .x = 436.0f, .y = 586.0f,
        .style = { .color = mel_vec4(0.08f, 0.11f, 0.14f, 1.0f) });
    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, S8("CUSTOM BADGE PROFILE"),
        .x = 232.0f, .y = 286.0f,
        .style = { .color = mel_vec4(0.16f, 0.14f, 0.08f, 1.0f) });
}

static void material_sprite_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(user);

    if (s_requested_policy_mode != s_policy_mode)
    {
        material_sprite_apply_policy(s_requested_policy_mode);
        bool ok = mel_render_stage_2d_rebuild(&s_stage);
        assert(ok);
    }

    s_time += dt;
    material_sprite_sync_viewport();
}

static void material_sprite_on_init(void)
{
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;
    Mel_Material_Family_Handle sprite = mel_material_family_find(S8("sprite"));

    material_sprite_register_backends();

    mel_render_list_init(&s_world_list,
        .name = S8("material_sprite_world"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());
    mel_render_list_init(&s_hud_text,
        .name = S8("material_sprite_hud"),
        .entry_stride = sizeof(Mel_Text_Entry),
        .alloc = mel_alloc_heap());

    s_world_camera = (Mel_Camera){
        .view = MEL_MAT4_IDENTITY,
        .projection = mel_mat4_ortho(0.0f, (f32)sc->extent.width, (f32)sc->extent.height, 0.0f, -1.0f, 1.0f),
    };
    s_hud_camera = s_world_camera;

    s_font = mel_font_atlas_pool_load(mel_font_pool(),
        .path = S8("/System/Library/Fonts/Monaco.ttf"),
        .size = 20.0f);

    s_card_template = mel_material_template_create(&(Mel_Material_Template_Desc){
        .name = S8("demo_card_template"),
        .family = sprite,
        .profile = S8("sprite.unlit"),
        .render_domain = MEL_MATERIAL_DOMAIN_OPAQUE,
        .fallback_policy = MEL_MATERIAL_FALLBACK_ALLOW,
        .base_color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
    });
    s_badge_template = mel_material_template_create(&(Mel_Material_Template_Desc){
        .name = S8("demo_badge_template"),
        .family = sprite,
        .profile = S8("sprite.badge"),
        .render_domain = MEL_MATERIAL_DOMAIN_OVERLAY,
        .fallback_policy = MEL_MATERIAL_FALLBACK_ALLOW,
        .base_color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
    });
    s_player_card = mel_material_instance_create(s_card_template);
    s_enemy_card = mel_material_instance_create(s_card_template);
    s_badge = mel_material_instance_create(s_badge_template);

    bool ok = mel_render_stage_2d_init(&s_stage,
        .name = S8("material_sprite"),
        .swapchain = s_swapchain_handle,
        .world_camera = &s_world_camera,
        .hud_camera = &s_hud_camera,
        .ui_camera = &s_hud_camera,
        .clear_color_enabled = true,
        .clear_color = mel_vec4(s_clear_color[0], s_clear_color[1], s_clear_color[2], s_clear_color[3]),
        .enable_imgui = true,
        .imgui_fn = material_sprite_imgui,
        .install_as_current_graph = true,
        .dev = mel_gpu_dev(),
        .sprite_pass = mel_sprite_pass(),
        .text_pass = mel_text_pass(),
        .alloc = mel_alloc_heap());
    assert(ok);

    mel_render_stage_2d_attach_sprite_list_to_layer(&s_stage, MEL_RENDER_STAGE_2D_LAYER_WORLD, &s_world_list);
    mel_render_stage_2d_attach_text_list_to_layer(&s_stage, MEL_RENDER_STAGE_2D_LAYER_HUD, &s_hud_text);

    material_sprite_apply_policy(MATERIAL_POLICY_DEFAULT);
    material_sprite_extract(nullptr, 0.0f, nullptr);
    ok = mel_render_stage_2d_rebuild(&s_stage);
    assert(ok);

    mel_imgui_init(s_window_handle, &mel_swapchain_registry_get(s_swapchain_handle)->swapchain);
}

void app_init(void)
{
    s_window_handle = mel_window_create(S8("Melody Sprite Materials"), .width = WIN_W, .height = WIN_H);
    s_swapchain_handle = mel_gpu_swapchain_create_for_window_ex(mel_gpu_dev(), s_window_handle,
        .preferred_present_mode = VK_PRESENT_MODE_MAILBOX_KHR);
    mel_vfs_mount_native(mel_vfs(), S8("/"), S8("/"), 0, false);

    material_sprite_on_init();

    mel_sim_init(&s_sim, .event_buffer = s_event_buf, .event_buffer_size = sizeof(s_event_buf));
    mel_sim_add_variable(&s_sim, material_sprite_update);
    mel_sim_add_variable(&s_sim, material_sprite_extract);
    mel_register_sim(&s_sim);
}

Mel_App_Config app_config(void)
{
    return (Mel_App_Config){
        .app_name = S8("Melody Sprite Materials"),
        .enable_validation = true,
    };
}

void app_shutdown(void)
{
    mel_unregister_sim(&s_sim);
    mel_sim_shutdown(&s_sim);

    Mel_Material_Family_Handle sprite = mel_material_family_find(S8("sprite"));
    mel_material_clear_family_policy(sprite);
    mel_render_stage_2d_shutdown(&s_stage);
    mel_material_instance_destroy(s_badge);
    mel_material_instance_destroy(s_enemy_card);
    mel_material_instance_destroy(s_player_card);
    mel_material_template_destroy(s_badge_template);
    mel_material_template_destroy(s_card_template);
    material_sprite_unregister_backends();
    mel_render_list_shutdown(&s_hud_text);
    mel_render_list_shutdown(&s_world_list);
    mel_vfs_unmount(mel_vfs(), S8("/"));
}

void app_event(SDL_Event* event)
{
    mel_process_event(event);

    if (event->type == SDL_EVENT_QUIT)
    {
        mel_quit();
        return;
    }

    if (event->type != SDL_EVENT_KEY_DOWN || event->key.repeat)
        return;

    switch (event->key.scancode)
    {
        case SDL_SCANCODE_ESCAPE:
            mel_quit();
            break;
        case SDL_SCANCODE_1:
            material_sprite_request_policy(MATERIAL_POLICY_DEFAULT);
            break;
        case SDL_SCANCODE_2:
            material_sprite_request_policy(MATERIAL_POLICY_STOCK);
            break;
        case SDL_SCANCODE_3:
            material_sprite_request_policy(MATERIAL_POLICY_CUSTOM);
            break;
        default:
            break;
    }
}
