#include <SDL3/SDL.h>
#include <tracy/TracyC.h>

#include <cimgui/cimgui.h>

#include "core.app.h"
#include "core.engine.h"
#include "window.h"
#include "swapchain.h"
#include "gpu.swapchain.h"
// ASYNC_V2: VFS removed
// #include "vfs.h"
#include "string.str8.h"
#include "render.stage.3d.h"
#include "render.view.h"
#include "render.list.h"
#include "render.camera.h"
#include "mesh.pass.h"
#include "sprite.pass.h"
#include "text.pass.h"
#include "text.draw.h"
#include "font.atlas.h"
#include "allocator.heap.h"
#include "math.mat4.h"
#include "math.vec2.h"
#include "math.vec3.h"
#include "math.vec4.h"
#include "sim.ctx.h"

#define WIN_W 1440
#define WIN_H 900
#define TAU_F 6.28318530718f

typedef enum {
    ACTOR_FLOOR = 0,
    ACTOR_TOWER,
    ACTOR_PLAYER,
    ACTOR_DRONE,
    ACTOR_BEACON,
    ACTOR_COUNT,
} Actor_Id;

typedef struct {
    u32 entry;
    Mel_Vec3 pos;
    Mel_Vec3 rot;
    Mel_Vec3 scale;
    Mel_Vec4 tint;
} Scene_Actor;

static Mel_Window_Handle s_window_handle;
static Mel_Swapchain_Handle s_swapchain_handle;
static Mel_Render_Stage_3D s_stage;
static Mel_Render_List s_world_meshes;
static Mel_Render_List s_hud_sprites;
static Mel_Render_List s_hud_text;
static Mel_Render_List s_debug_sprites;
static Mel_Camera s_world_camera;
static Mel_Camera s_overlay_camera;
static Mel_Sim_Ctx s_sim;
static u8 s_event_buf[4096];
static Mel_Font_Handle s_font;
static Scene_Actor s_actors[ACTOR_COUNT];

static f32 s_time;
static f32 s_orbit_speed = 0.45f;
static f32 s_camera_distance = 8.5f;
static f32 s_camera_height = 4.2f;
static bool s_pause_animation;
static bool s_show_minimap = true;
static bool s_show_reticle = true;
static bool s_show_beacon = true;
static float s_scene_clear[4] = { 0.05f, 0.06f, 0.08f, 1.0f };
static float s_scene_tint[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
static float s_hud_accent[4] = { 0.35f, 0.90f, 0.78f, 1.0f };

static const Mel_Vec3 s_cube_positions[] = {
    { .x = -1.0f, .y = -1.0f, .z =  1.0f },
    { .x =  1.0f, .y = -1.0f, .z =  1.0f },
    { .x =  1.0f, .y =  1.0f, .z =  1.0f },
    { .x = -1.0f, .y =  1.0f, .z =  1.0f },

    { .x =  1.0f, .y = -1.0f, .z = -1.0f },
    { .x = -1.0f, .y = -1.0f, .z = -1.0f },
    { .x = -1.0f, .y =  1.0f, .z = -1.0f },
    { .x =  1.0f, .y =  1.0f, .z = -1.0f },

    { .x = -1.0f, .y = -1.0f, .z = -1.0f },
    { .x = -1.0f, .y = -1.0f, .z =  1.0f },
    { .x = -1.0f, .y =  1.0f, .z =  1.0f },
    { .x = -1.0f, .y =  1.0f, .z = -1.0f },

    { .x =  1.0f, .y = -1.0f, .z =  1.0f },
    { .x =  1.0f, .y = -1.0f, .z = -1.0f },
    { .x =  1.0f, .y =  1.0f, .z = -1.0f },
    { .x =  1.0f, .y =  1.0f, .z =  1.0f },

    { .x = -1.0f, .y =  1.0f, .z =  1.0f },
    { .x =  1.0f, .y =  1.0f, .z =  1.0f },
    { .x =  1.0f, .y =  1.0f, .z = -1.0f },
    { .x = -1.0f, .y =  1.0f, .z = -1.0f },

    { .x = -1.0f, .y = -1.0f, .z = -1.0f },
    { .x =  1.0f, .y = -1.0f, .z = -1.0f },
    { .x =  1.0f, .y = -1.0f, .z =  1.0f },
    { .x = -1.0f, .y = -1.0f, .z =  1.0f },
};

static const Mel_Vec4 s_cube_colors[] = {
    { .x = 0.90f, .y = 0.32f, .z = 0.28f, .w = 1.0f },
    { .x = 0.90f, .y = 0.32f, .z = 0.28f, .w = 1.0f },
    { .x = 0.90f, .y = 0.32f, .z = 0.28f, .w = 1.0f },
    { .x = 0.90f, .y = 0.32f, .z = 0.28f, .w = 1.0f },

    { .x = 0.23f, .y = 0.52f, .z = 0.92f, .w = 1.0f },
    { .x = 0.23f, .y = 0.52f, .z = 0.92f, .w = 1.0f },
    { .x = 0.23f, .y = 0.52f, .z = 0.92f, .w = 1.0f },
    { .x = 0.23f, .y = 0.52f, .z = 0.92f, .w = 1.0f },

    { .x = 0.18f, .y = 0.78f, .z = 0.48f, .w = 1.0f },
    { .x = 0.18f, .y = 0.78f, .z = 0.48f, .w = 1.0f },
    { .x = 0.18f, .y = 0.78f, .z = 0.48f, .w = 1.0f },
    { .x = 0.18f, .y = 0.78f, .z = 0.48f, .w = 1.0f },

    { .x = 0.95f, .y = 0.72f, .z = 0.22f, .w = 1.0f },
    { .x = 0.95f, .y = 0.72f, .z = 0.22f, .w = 1.0f },
    { .x = 0.95f, .y = 0.72f, .z = 0.22f, .w = 1.0f },
    { .x = 0.95f, .y = 0.72f, .z = 0.22f, .w = 1.0f },

    { .x = 0.64f, .y = 0.42f, .z = 0.90f, .w = 1.0f },
    { .x = 0.64f, .y = 0.42f, .z = 0.90f, .w = 1.0f },
    { .x = 0.64f, .y = 0.42f, .z = 0.90f, .w = 1.0f },
    { .x = 0.64f, .y = 0.42f, .z = 0.90f, .w = 1.0f },

    { .x = 0.20f, .y = 0.82f, .z = 0.88f, .w = 1.0f },
    { .x = 0.20f, .y = 0.82f, .z = 0.88f, .w = 1.0f },
    { .x = 0.20f, .y = 0.82f, .z = 0.88f, .w = 1.0f },
    { .x = 0.20f, .y = 0.82f, .z = 0.88f, .w = 1.0f },
};

static const u32 s_cube_indices[] = {
    0, 1, 2, 2, 3, 0,
    4, 5, 6, 6, 7, 4,
    8, 9, 10, 10, 11, 8,
    12, 13, 14, 14, 15, 12,
    16, 17, 18, 18, 19, 16,
    20, 21, 22, 22, 23, 20,
};

static const Mel_Mesh s_cube_mesh = {
    .positions = s_cube_positions,
    .colors = s_cube_colors,
    .vertex_count = SDL_arraysize(s_cube_positions),
    .indices = s_cube_indices,
    .index_count = SDL_arraysize(s_cube_indices),
};

static Mel_Mat4 scene3d_transform(Mel_Vec3 pos, Mel_Vec3 rot, Mel_Vec3 scale)
{
    Mel_Mat4 t = mel_mat4_translate(pos);
    Mel_Mat4 r = mel_mat4_rotateXYZ(rot.x, rot.y, rot.z);
    Mel_Mat4 s = mel_mat4_scale(scale);
    return mel_mat4_mul(mel_mat4_mul(t, r), s);
}

static Mel_Vec4 scene3d_scene_tint(Mel_Vec4 color)
{
    Mel_Vec4 tint = mel_vec4(s_scene_tint[0], s_scene_tint[1], s_scene_tint[2], s_scene_tint[3]);
    return mel_vec4_mul(color, tint);
}

static void scene3d_draw_panel(Mel_Render_List* list, f32 x, f32 y, f32 w, f32 h, Mel_Vec4 color)
{
    mel_draw_sprite(list,
        .pos = mel_vec2(x, y),
        .size = mel_vec2(w, h),
        .color = color,
        .tex = (Mel_Texture_Handle){0},
        .uv = MEL_UV_FULL);
}

static void scene3d_draw_marker(Mel_Render_List* list, f32 x, f32 y, f32 size, Mel_Vec4 color)
{
    mel_draw_sprite(list,
        .pos = mel_vec2(x, y),
        .size = mel_vec2(size, size),
        .color = color,
        .tex = (Mel_Texture_Handle){0},
        .uv = MEL_UV_FULL);
}

static void scene3d_sync_viewport(void)
{
    TracyCZoneN(ctx, "scene3d_sync_viewport", true);
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;
    Mel_View_Handle world_view = mel_render_stage_3d_view(&s_stage, MEL_RENDER_STAGE_3D_LAYER_WORLD);
    i32 w = 0;
    i32 h = 0;
    mel_window_size_pixels(s_window_handle, &w, &h);
    if (w <= 0 || h <= 0)
    {
        TracyCZoneEnd(ctx);
        return;
    }

    s_overlay_camera.view = MEL_MAT4_IDENTITY;
    s_overlay_camera.projection = mel_mat4_ortho(0.0f, (f32)w, (f32)h, 0.0f, -1.0f, 1.0f);
    mel_view_set_clear_color_enabled(world_view, true);
    mel_view_set_clear_color(world_view,
        mel_vec4(s_scene_clear[0], s_scene_clear[1], s_scene_clear[2], s_scene_clear[3]));

    if (sc->extent.width != (u32)w || sc->extent.height != (u32)h)
    {
        mel_swapchain_resize(sc, mel_gpu_dev(), (u32)w, (u32)h);
        s_world_camera.projection = mel_mat4_perspective(60.0f * (3.14159265f / 180.0f),
            (f32)w / (f32)h, 0.1f, 100.0f);
    }

    bool ok = mel_render_stage_3d_refresh(&s_stage);
    assert(ok);
    MEL_UNUSED(ok);
    TracyCZoneEnd(ctx);
}

static void scene3d_tools_imgui(void* user)
{
    MEL_UNUSED(user);
    TracyCZoneN(ctx, "scene3d_imgui", true);
    Mel_Frame_Stats frame = mel_frame_stats();
    Mel_Sim_Stats sim = mel_sim_stats(&s_sim);

    igSetNextWindowPos((ImVec2){ 24.0f, 24.0f }, ImGuiCond_FirstUseEver, (ImVec2){0});
    igSetNextWindowSize((ImVec2){ 360.0f, 420.0f }, ImGuiCond_FirstUseEver);
    igSetNextWindowBgAlpha(0.94f);
    if (igBegin("Scene Tools", nullptr, 0))
    {
        igText("Stage-driven 3D scene with HUD/debug/imgui layers.");
        igSeparator();

        igCheckbox("Pause Animation", &s_pause_animation);
        igCheckbox("Show Minimap", &s_show_minimap);
        igCheckbox("Show Reticle", &s_show_reticle);
        igCheckbox("Show Beacon", &s_show_beacon);

        igSliderFloat("Orbit Speed", &s_orbit_speed, 0.0f, 1.8f, "%.2f", 0);
        igSliderFloat("Camera Distance", &s_camera_distance, 4.0f, 14.0f, "%.2f", 0);
        igSliderFloat("Camera Height", &s_camera_height, 1.0f, 8.0f, "%.2f", 0);
        igColorEdit4("Scene Clear", s_scene_clear, 0);
        igColorEdit4("Scene Tint", s_scene_tint, 0);
        igColorEdit4("HUD Accent", s_hud_accent, 0);

        igSeparator();
        igText("World: mesh technique");
        igText("HUD: sprite + text overlays");
        igText("Debug: sprite-backed reticle");
        igText("Tools: imgui technique");
        igSeparator();

        igText("Frame: %.2f ms", frame.dt * 1000.0f);
        igText("FPS: %.1f", frame.fps);
        igText("Sim dt: %.2f ms", sim.last_scaled_dt * 1000.0f);
        igText("Fixed steps: %u", sim.fixed_steps);
        igText("Camera Dist: %.2f", s_camera_distance);
    }
    igEnd();
    TracyCZoneEnd(ctx);
}

static void scene3d_world_init(void)
{
    for (u32 i = 0; i < ACTOR_COUNT; i++)
        s_actors[i].entry = mel_render_list_insert(&s_world_meshes, mel_sort_key_mesh_opaque(0.0f));

    Mel_Mesh_Entry* floor = mel_render_list_get(&s_world_meshes, s_actors[ACTOR_FLOOR].entry);
    *floor = (Mel_Mesh_Entry){
        .mesh = &s_cube_mesh,
        .transform = MEL_MAT4_IDENTITY,
        .color = mel_vec4(0.66f, 0.72f, 0.80f, 1.0f),
    };

    Mel_Mesh_Entry* tower = mel_render_list_get(&s_world_meshes, s_actors[ACTOR_TOWER].entry);
    *tower = (Mel_Mesh_Entry){
        .mesh = &s_cube_mesh,
        .transform = MEL_MAT4_IDENTITY,
        .color = mel_vec4(0.75f, 0.54f, 0.26f, 1.0f),
    };

    Mel_Mesh_Entry* player = mel_render_list_get(&s_world_meshes, s_actors[ACTOR_PLAYER].entry);
    *player = (Mel_Mesh_Entry){
        .mesh = &s_cube_mesh,
        .transform = MEL_MAT4_IDENTITY,
        .color = mel_vec4(0.90f, 0.88f, 0.80f, 1.0f),
    };

    Mel_Mesh_Entry* drone = mel_render_list_get(&s_world_meshes, s_actors[ACTOR_DRONE].entry);
    *drone = (Mel_Mesh_Entry){
        .mesh = &s_cube_mesh,
        .transform = MEL_MAT4_IDENTITY,
        .color = mel_vec4(0.60f, 0.84f, 0.96f, 1.0f),
    };

    Mel_Mesh_Entry* beacon = mel_render_list_get(&s_world_meshes, s_actors[ACTOR_BEACON].entry);
    *beacon = (Mel_Mesh_Entry){
        .mesh = &s_cube_mesh,
        .transform = MEL_MAT4_IDENTITY,
        .color = mel_vec4(1.00f, 0.42f, 0.44f, 1.0f),
    };
}

static void scene3d_update_camera(void)
{
    f32 angle = s_time * s_orbit_speed;
    Mel_Vec3 eye = mel_vec3(cosf(angle) * s_camera_distance,
        s_camera_height,
        sinf(angle) * s_camera_distance);
    Mel_Vec3 target = mel_vec3(0.0f, 0.0f, 0.0f);
    s_world_camera.view = mel_mat4_look_at(eye, target, MEL_VEC3_UP);
    s_world_camera.position = eye;
}

static void scene3d_simulate(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(user);
    TracyCZoneN(ctx, "scene3d_simulate", true);

    scene3d_sync_viewport();

    if (!s_pause_animation)
        s_time += dt;

    scene3d_update_camera();

    s_actors[ACTOR_FLOOR].pos = mel_vec3(0.0f, -1.45f, 0.0f);
    s_actors[ACTOR_FLOOR].rot = mel_vec3(0.0f, 0.0f, 0.0f);
    s_actors[ACTOR_FLOOR].scale = mel_vec3(6.5f, 0.18f, 6.5f);
    s_actors[ACTOR_FLOOR].tint = scene3d_scene_tint(mel_vec4(0.70f, 0.76f, 0.82f, 1.0f));

    s_actors[ACTOR_TOWER].pos = mel_vec3(-2.40f, 0.35f, -1.80f);
    s_actors[ACTOR_TOWER].rot = mel_vec3(0.0f, s_time * 0.15f, 0.0f);
    s_actors[ACTOR_TOWER].scale = mel_vec3(0.90f, 2.90f, 0.90f);
    s_actors[ACTOR_TOWER].tint = scene3d_scene_tint(mel_vec4(0.88f, 0.66f, 0.34f, 1.0f));

    s_actors[ACTOR_PLAYER].pos = mel_vec3(0.55f, -0.25f + sinf(s_time * 2.4f) * 0.10f, 0.45f);
    s_actors[ACTOR_PLAYER].rot = mel_vec3(0.0f, s_time * 1.8f, 0.0f);
    s_actors[ACTOR_PLAYER].scale = mel_vec3(0.95f, 1.25f, 0.95f);
    s_actors[ACTOR_PLAYER].tint = scene3d_scene_tint(mel_vec4(0.98f, 0.96f, 0.82f, 1.0f));

    s_actors[ACTOR_DRONE].pos = mel_vec3(cosf(s_time * 1.4f) * 2.10f,
        1.70f + sinf(s_time * 2.3f) * 0.22f,
        sinf(s_time * 1.4f) * 1.55f);
    s_actors[ACTOR_DRONE].rot = mel_vec3(s_time * 1.1f, s_time * 2.6f, 0.0f);
    s_actors[ACTOR_DRONE].scale = mel_vec3(0.42f, 0.42f, 0.42f);
    s_actors[ACTOR_DRONE].tint = scene3d_scene_tint(mel_vec4(0.68f, 0.88f, 1.00f, 1.0f));

    s_actors[ACTOR_BEACON].pos = s_show_beacon ? mel_vec3(2.45f, -0.55f, 1.85f) : mel_vec3(2.45f, -20.0f, 1.85f);
    s_actors[ACTOR_BEACON].rot = mel_vec3(0.0f, s_time * 3.0f, 0.0f);
    f32 beacon_scale = 0.55f + 0.08f * (0.5f + 0.5f * sinf(s_time * 4.0f));
    s_actors[ACTOR_BEACON].scale = mel_vec3(beacon_scale, beacon_scale * 1.8f, beacon_scale);
    s_actors[ACTOR_BEACON].tint = scene3d_scene_tint(mel_vec4(1.00f, 0.48f, 0.52f, 1.0f));

    for (u32 i = 0; i < ACTOR_COUNT; i++)
    {
        Mel_Mesh_Entry* entry = mel_render_list_get(&s_world_meshes, s_actors[i].entry);
        entry->transform = scene3d_transform(s_actors[i].pos, s_actors[i].rot, s_actors[i].scale);
        entry->color = s_actors[i].tint;
    }
    TracyCZoneEnd(ctx);
}

static void scene3d_extract(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(dt);
    MEL_UNUSED(user);
    TracyCZoneN(ctx, "scene3d_extract", true);

    mel_render_list_clear(&s_hud_sprites);
    mel_render_list_clear(&s_hud_text);
    mel_render_list_clear(&s_debug_sprites);

    i32 viewport_w = 0;
    i32 viewport_h = 0;
    mel_window_size_pixels(s_window_handle, &viewport_w, &viewport_h);
    f32 hud_w = (f32)viewport_w;
    f32 hud_h = (f32)viewport_h;
    Mel_Vec4 accent = mel_vec4(s_hud_accent[0], s_hud_accent[1], s_hud_accent[2], s_hud_accent[3]);
    Mel_Vec4 panel = mel_vec4(accent.x * 0.16f, accent.y * 0.16f, accent.z * 0.18f, 0.84f);
    Mel_Vec4 subpanel = mel_vec4(0.08f, 0.10f, 0.14f, 0.80f);
    Mel_Text_Style title_style = mel_text_style(mel_vec4(0.94f, 0.97f, 0.99f, 1.0f));
    Mel_Text_Style body_style = mel_text_style(mel_vec4(0.82f, 0.88f, 0.93f, 1.0f));
    title_style.outline = 0.12f;
    title_style.outline_color = mel_vec4(0.05f, 0.06f, 0.08f, 1.0f);

    scene3d_draw_panel(&s_hud_sprites, 28.0f, 28.0f, 360.0f, 152.0f, panel);
    scene3d_draw_panel(&s_hud_sprites, 28.0f, hud_h - 96.0f, 420.0f, 64.0f, subpanel);

    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, S8("SECTOR DELTA / LIVE"),
        .x = 52.0f, .y = 44.0f, .style = title_style);
    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text,
        S8("Objective: keep the drone in orbit,\nhold the beacon online, and monitor the tool feed."),
        .x = 52.0f, .y = 82.0f, .style = body_style);

    char stats_buf[256];
    Mel_Frame_Stats frame = mel_frame_stats();
    SDL_snprintf(stats_buf, sizeof(stats_buf),
        "cam %.1fm  orbit %.2fx  fps %.0f",
        s_camera_distance, s_orbit_speed, frame.fps);
    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, str8_from_cstr(stats_buf),
        .x = 52.0f, .y = hud_h - 78.0f, .style = body_style);

    if (s_show_minimap)
    {
        f32 map_w = 250.0f;
        f32 map_h = 180.0f;
        f32 map_x = hud_w - map_w - 32.0f;
        f32 map_y = 28.0f;
        scene3d_draw_panel(&s_hud_sprites, map_x, map_y, map_w, map_h, subpanel);
        mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, S8("TACTICAL OVERLAY"),
            .x = map_x + 18.0f, .y = map_y + 16.0f, .style = title_style);

        f32 inner_x = map_x + 18.0f;
        f32 inner_y = map_y + 48.0f;
        f32 inner_w = map_w - 36.0f;
        f32 inner_h = map_h - 66.0f;
        scene3d_draw_panel(&s_hud_sprites, inner_x, inner_y, inner_w, inner_h,
            mel_vec4(0.05f, 0.08f, 0.10f, 0.92f));

        for (u32 i = 0; i < ACTOR_COUNT; i++)
        {
            if (!s_show_beacon && i == ACTOR_BEACON)
                continue;

            f32 u = 0.5f + s_actors[i].pos.x / 7.0f;
            f32 v = 0.5f + s_actors[i].pos.z / 7.0f;
            f32 px = inner_x + u * inner_w;
            f32 py = inner_y + v * inner_h;
            scene3d_draw_marker(&s_hud_sprites, px - 5.0f, py - 5.0f, 10.0f, s_actors[i].tint);
        }
    }

    if (s_show_reticle)
    {
        f32 cx = hud_w * 0.5f;
        f32 cy = hud_h * 0.5f;
        Mel_Vec4 reticle = mel_vec4(accent.x, accent.y, accent.z, 0.95f);
        scene3d_draw_panel(&s_debug_sprites, cx - 36.0f, cy - 1.0f, 20.0f, 2.0f, reticle);
        scene3d_draw_panel(&s_debug_sprites, cx + 16.0f, cy - 1.0f, 20.0f, 2.0f, reticle);
        scene3d_draw_panel(&s_debug_sprites, cx - 1.0f, cy - 36.0f, 2.0f, 20.0f, reticle);
        scene3d_draw_panel(&s_debug_sprites, cx - 1.0f, cy + 16.0f, 2.0f, 20.0f, reticle);
    }
    TracyCZoneEnd(ctx);
}

static void scene3d_on_init(void)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;

    mel_vfs_mount_native(mel_vfs(), S8("/fonts"), S8("/System/Library/Fonts"), 0, false);

    mel_render_list_init(&s_world_meshes,
        .name = S8("scene3d_world"),
        .entry_stride = sizeof(Mel_Mesh_Entry),
        .alloc = mel_alloc_heap());
    mel_render_list_init(&s_hud_sprites,
        .name = S8("scene3d_hud_sprites"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());
    mel_render_list_init(&s_hud_text,
        .name = S8("scene3d_hud_text"),
        .entry_stride = sizeof(Mel_Text_Entry),
        .alloc = mel_alloc_heap());
    mel_render_list_init(&s_debug_sprites,
        .name = S8("scene3d_debug"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    s_world_camera = (Mel_Camera){
        .projection = mel_mat4_perspective(60.0f * (3.14159265f / 180.0f),
            (f32)sc->extent.width / (f32)sc->extent.height, 0.1f, 100.0f),
    };
    s_overlay_camera = (Mel_Camera){
        .view = MEL_MAT4_IDENTITY,
        .projection = mel_mat4_ortho(0.0f, (f32)sc->extent.width, (f32)sc->extent.height, 0.0f, -1.0f, 1.0f),
    };
    scene3d_update_camera();

    s_font = mel_font_atlas_pool_load(mel_font_pool(),
        .path = S8("/fonts/Monaco.ttf"),
        .size = 20.0f);

    bool ok = mel_render_stage_3d_init(&s_stage,
        .name = S8("scene3d_tools"),
        .swapchain = s_swapchain_handle,
        .world_camera = &s_world_camera,
        .hud_camera = &s_overlay_camera,
        .debug_camera = &s_overlay_camera,
        .ui_camera = &s_overlay_camera,
        .clear_color_enabled = true,
        .clear_color = mel_vec4(s_scene_clear[0], s_scene_clear[1], s_scene_clear[2], s_scene_clear[3]),
        .enable_imgui = true,
        .imgui_fn = scene3d_tools_imgui,
        .install_as_current_graph = true,
        .dev = dev,
        .mesh_pass = mel_mesh_pass(),
        .sprite_pass = mel_sprite_pass(),
        .text_pass = mel_text_pass(),
        .alloc = mel_alloc_heap());
    assert(ok);

    mel_render_stage_3d_attach_mesh_list(&s_stage, &s_world_meshes);
    mel_render_stage_3d_attach_sprite_list_to_layer(&s_stage, MEL_RENDER_STAGE_3D_LAYER_HUD, &s_hud_sprites);
    mel_render_stage_3d_attach_text_list_to_layer(&s_stage, MEL_RENDER_STAGE_3D_LAYER_HUD, &s_hud_text);
    mel_render_stage_3d_attach_sprite_list_to_layer(&s_stage, MEL_RENDER_STAGE_3D_LAYER_DEBUG, &s_debug_sprites);
    ok = mel_render_stage_3d_rebuild(&s_stage);
    assert(ok);

    scene3d_world_init();
    scene3d_extract(nullptr, 0.0f, nullptr);
    mel_imgui_init(s_window_handle, &mel_swapchain_registry_get(s_swapchain_handle)->swapchain);
}

void app_init(void)
{
    s_window_handle = mel_window_create(S8("Melody Scene 3D Tools"), .width = WIN_W, .height = WIN_H);
    s_swapchain_handle = mel_gpu_swapchain_create_for_window_ex(mel_gpu_dev(), s_window_handle,
        .preferred_present_mode = VK_PRESENT_MODE_MAILBOX_KHR);

    scene3d_on_init();

    mel_sim_init(&s_sim, .event_buffer = s_event_buf, .event_buffer_size = sizeof(s_event_buf));
    mel_sim_add_variable(&s_sim, scene3d_simulate);
    mel_sim_add_variable(&s_sim, scene3d_extract);
    mel_register_sim(&s_sim);
}

Mel_App_Config app_config(void)
{
    return (Mel_App_Config){
        .app_name = S8("Melody Scene 3D Tools"),
        .enable_validation = true,
    };
}

void app_shutdown(void)
{
    mel_unregister_sim(&s_sim);
    mel_sim_shutdown(&s_sim);
    mel_render_stage_3d_shutdown(&s_stage);
    mel_render_list_shutdown(&s_debug_sprites);
    mel_render_list_shutdown(&s_hud_text);
    mel_render_list_shutdown(&s_hud_sprites);
    mel_render_list_shutdown(&s_world_meshes);
}

void app_event(SDL_Event* event)
{
    mel_process_event(event);

    if (event->type == SDL_EVENT_QUIT)
        mel_quit();
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE)
        mel_quit();
}
