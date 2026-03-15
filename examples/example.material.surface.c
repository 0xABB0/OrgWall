#include <SDL3/SDL.h>

#include <cimgui/cimgui.h>

#include "core.app.h"
#include "core.engine.h"
#include "window.h"
#include "swapchain.h"
#include "gpu.swapchain.h"
#include "vfs.h"
#include "vfs.backend.os.h"
#include "string.str8.h"
#include "render.stage.3d.h"
#include "render.frame_plan.h"
#include "render.list.h"
#include "render.camera.h"
#include "render.view.h"
#include "render.material.h"
#include "mesh.pass.h"
#include "text.pass.h"
#include "text.draw.h"
#include "font.atlas.h"
#include "allocator.heap.h"
#include "math.mat4.h"
#include "math.vec3.h"
#include "math.vec4.h"
#include "sim.ctx.h"

#define WIN_W 1280
#define WIN_H 800

static Mel_Window_Handle s_window_handle;
static Mel_Swapchain_Handle s_swapchain_handle;
static Mel_Render_Stage_3D s_stage;
static Mel_Render_List s_meshes;
static Mel_Render_List s_hud_text;
static Mel_Camera s_world_camera;
static Mel_Camera s_overlay_camera;
static Mel_Sim_Ctx s_sim;
static u8 s_event_buf[4096];
static Mel_Font_Handle s_font;
static Mel_Material_Template_Handle s_unlit_template;
static Mel_Material_Template_Handle s_standard_template;
static Mel_Material_Instance_Handle s_unlit_material;
static Mel_Material_Instance_Handle s_standard_material;
static u32 s_unlit_cube_entry;
static u32 s_standard_cube_entry;
static float s_unlit_color[4] = { 0.25f, 0.73f, 0.95f, 1.0f };
static float s_standard_color[4] = { 0.97f, 0.79f, 0.31f, 1.0f };
static float s_standard_roughness = 0.35f;
static float s_standard_metallic = 0.15f;
static float s_standard_occlusion = 1.0f;
static float s_standard_emissive[4] = { 0.08f, 0.06f, 0.02f, 0.55f };
static float s_clear_color[4] = { 0.06f, 0.07f, 0.10f, 1.0f };
static float s_light_dir[3] = { 0.55f, -1.0f, 0.35f };
static float s_light_color[4] = { 0.95f, 0.97f, 1.0f, 1.0f };
static float s_ambient = 0.18f;
static float s_time;

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

static const Mel_Vec3 s_cube_normals[] = {
    { .x =  0.0f, .y =  0.0f, .z =  1.0f },
    { .x =  0.0f, .y =  0.0f, .z =  1.0f },
    { .x =  0.0f, .y =  0.0f, .z =  1.0f },
    { .x =  0.0f, .y =  0.0f, .z =  1.0f },

    { .x =  0.0f, .y =  0.0f, .z = -1.0f },
    { .x =  0.0f, .y =  0.0f, .z = -1.0f },
    { .x =  0.0f, .y =  0.0f, .z = -1.0f },
    { .x =  0.0f, .y =  0.0f, .z = -1.0f },

    { .x = -1.0f, .y =  0.0f, .z =  0.0f },
    { .x = -1.0f, .y =  0.0f, .z =  0.0f },
    { .x = -1.0f, .y =  0.0f, .z =  0.0f },
    { .x = -1.0f, .y =  0.0f, .z =  0.0f },

    { .x =  1.0f, .y =  0.0f, .z =  0.0f },
    { .x =  1.0f, .y =  0.0f, .z =  0.0f },
    { .x =  1.0f, .y =  0.0f, .z =  0.0f },
    { .x =  1.0f, .y =  0.0f, .z =  0.0f },

    { .x =  0.0f, .y =  1.0f, .z =  0.0f },
    { .x =  0.0f, .y =  1.0f, .z =  0.0f },
    { .x =  0.0f, .y =  1.0f, .z =  0.0f },
    { .x =  0.0f, .y =  1.0f, .z =  0.0f },

    { .x =  0.0f, .y = -1.0f, .z =  0.0f },
    { .x =  0.0f, .y = -1.0f, .z =  0.0f },
    { .x =  0.0f, .y = -1.0f, .z =  0.0f },
    { .x =  0.0f, .y = -1.0f, .z =  0.0f },
};

static const Mel_Vec4 s_cube_colors[] = {
    { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f }, { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f },
    { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f }, { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f },
    { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f }, { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f },
    { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f }, { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f },
    { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f }, { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f },
    { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f }, { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f },
    { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f }, { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f },
    { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f }, { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f },
    { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f }, { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f },
    { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f }, { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f },
    { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f }, { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f },
    { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f }, { .x = 1.0f, .y = 1.0f, .z = 1.0f, .w = 1.0f },
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
    .normals = s_cube_normals,
    .colors = s_cube_colors,
    .vertex_count = SDL_arraysize(s_cube_positions),
    .indices = s_cube_indices,
    .index_count = SDL_arraysize(s_cube_indices),
};

static void material_surface_sync_viewport(void)
{
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;
    Mel_View_Handle world_view = mel_render_stage_3d_view(&s_stage, MEL_RENDER_STAGE_3D_LAYER_WORLD);
    i32 w = 0;
    i32 h = 0;
    mel_window_size_pixels(s_window_handle, &w, &h);
    if (w <= 0 || h <= 0)
        return;

    mel_view_set_clear_color_enabled(world_view, true);
    mel_view_set_clear_color(world_view,
        mel_vec4(s_clear_color[0], s_clear_color[1], s_clear_color[2], s_clear_color[3]));

    if (sc->extent.width != (u32)w || sc->extent.height != (u32)h)
    {
        mel_swapchain_resize(sc, mel_gpu_dev(), (u32)w, (u32)h);
        s_world_camera.projection = mel_mat4_perspective(60.0f * (3.14159265f / 180.0f),
            (f32)w / (f32)h, 0.1f, 100.0f);
        s_overlay_camera.projection = mel_mat4_ortho(0.0f, (f32)w, (f32)h, 0.0f, -1.0f, 1.0f);
    }

    bool ok = mel_render_stage_3d_refresh(&s_stage);
    assert(ok);
}

static str8 material_surface_resolved_backend(Mel_Material_Instance_Handle material)
{
    Mel_Frame_Plan_Handle plan = mel_render_stage_3d_plan(&s_stage);
    Mel_View_Handle world_view = mel_render_stage_3d_view(&s_stage, MEL_RENDER_STAGE_3D_LAYER_WORLD);
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

static void material_surface_imgui(void* user)
{
    MEL_UNUSED(user);

    str8 unlit = material_surface_resolved_backend(s_unlit_material);
    str8 standard = material_surface_resolved_backend(s_standard_material);

    igSetNextWindowPos((ImVec2){ 860.0f, 24.0f }, ImGuiCond_Once, (ImVec2){0, 0});
    igSetNextWindowSize((ImVec2){ 380.0f, 360.0f }, ImGuiCond_Once);
    if (igBegin("Surface Materials", nullptr, 0))
    {
        igTextWrapped("Left cube uses surface.unlit. Right cube uses surface.standard.forward through the same mesh.forward technique.");
        igSeparator();
        igText("Unlit backend: %.*s", (int)unlit.len, unlit.data);
        igText("Standard backend: %.*s", (int)standard.len, standard.data);
        igSeparator();
        igColorEdit4("Unlit Color", s_unlit_color, 0);
        igColorEdit4("Standard Color", s_standard_color, 0);
        igSliderFloat("Standard Roughness", &s_standard_roughness, 0.0f, 1.0f, "%.2f", 0);
        igSliderFloat("Standard Metallic", &s_standard_metallic, 0.0f, 1.0f, "%.2f", 0);
        igSliderFloat("Standard Occlusion", &s_standard_occlusion, 0.0f, 1.0f, "%.2f", 0);
        igColorEdit4("Standard Emissive", s_standard_emissive, 0);
        igColorEdit4("Light Color", s_light_color, 0);
        igColorEdit4("Background", s_clear_color, 0);
        igSliderFloat3("Light Direction", s_light_dir, -1.0f, 1.0f, "%.2f", 0);
        igSliderFloat("Ambient", &s_ambient, 0.0f, 0.8f, "%.2f", 0);
    }
    igEnd();
}

static void material_surface_extract(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(dt);
    MEL_UNUSED(user);

    mel_material_instance_set_base_color(s_unlit_material,
        mel_vec4(s_unlit_color[0], s_unlit_color[1], s_unlit_color[2], s_unlit_color[3]));
    mel_material_instance_set_base_color(s_standard_material,
        mel_vec4(s_standard_color[0], s_standard_color[1], s_standard_color[2], s_standard_color[3]));
    mel_material_instance_set_f32(s_standard_material, S8("roughness"), s_standard_roughness);
    mel_material_instance_set_f32(s_standard_material, S8("metallic"), s_standard_metallic);
    mel_material_instance_set_f32(s_standard_material, S8("occlusion"), s_standard_occlusion);
    mel_material_instance_set_vec4(s_standard_material, S8("emissive"),
        mel_vec4(s_standard_emissive[0], s_standard_emissive[1], s_standard_emissive[2], s_standard_emissive[3]));

    mel_render_list_clear(&s_hud_text);

    str8 unlit = material_surface_resolved_backend(s_unlit_material);
    str8 standard = material_surface_resolved_backend(s_standard_material);
    char line_a[160];
    char line_b[160];
    SDL_snprintf(line_a, sizeof(line_a), "surface.unlit -> %.*s", (int)unlit.len, unlit.data);
    SDL_snprintf(line_b, sizeof(line_b), "surface.standard -> %.*s", (int)standard.len, standard.data);

    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, S8("Surface Material Demo"),
        .x = 56.0f, .y = 748.0f,
        .style = { .color = mel_vec4(0.95f, 0.96f, 0.98f, 1.0f) });
    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text,
        S8("Same mesh technique, different material backends. Standard forward adds simple directional lighting."),
        .x = 56.0f, .y = 716.0f,
        .style = { .color = mel_vec4(0.74f, 0.78f, 0.84f, 1.0f) });
    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, str8_from_cstr(line_a),
        .x = 56.0f, .y = 684.0f,
        .style = { .color = mel_vec4(0.70f, 0.86f, 0.98f, 1.0f) });
    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, str8_from_cstr(line_b),
        .x = 56.0f, .y = 656.0f,
        .style = { .color = mel_vec4(0.95f, 0.84f, 0.38f, 1.0f) });
    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, S8("UNLIT"),
        .x = 314.0f, .y = 148.0f,
        .style = { .color = mel_vec4(0.83f, 0.90f, 0.98f, 1.0f) });
    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, S8("STANDARD FORWARD"),
        .x = 748.0f, .y = 148.0f,
        .style = { .color = mel_vec4(0.98f, 0.90f, 0.64f, 1.0f) });
}

static void material_surface_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(user);

    s_time += dt;
    material_surface_sync_viewport();

    mel_mesh_pass_set_lighting(mel_mesh_pass(), (Mel_Mesh_Lighting){
        .direction = mel_vec3(s_light_dir[0], s_light_dir[1], s_light_dir[2]),
        .color = mel_vec4(s_light_color[0], s_light_color[1], s_light_color[2], s_light_color[3]),
        .ambient = s_ambient,
    });

    Mel_Mat4 left_transform = mel_mat4_mul(
        mel_mat4_translate(mel_vec3(-1.8f, 0.0f, 0.0f)),
        mel_mat4_mul(
            mel_mat4_rotateXYZ(s_time * 0.7f, s_time * 1.0f, 0.0f),
            mel_mat4_scalef(0.85f)));
    Mel_Mat4 right_transform = mel_mat4_mul(
        mel_mat4_translate(mel_vec3(1.8f, 0.0f, 0.0f)),
        mel_mat4_mul(
            mel_mat4_rotateXYZ(s_time * 0.7f, s_time * 1.0f, 0.0f),
            mel_mat4_scalef(0.85f)));

    Mel_Mesh_Entry* unlit_cube = mel_render_list_get(&s_meshes, s_unlit_cube_entry);
    unlit_cube->transform = left_transform;
    Mel_Mesh_Entry* standard_cube = mel_render_list_get(&s_meshes, s_standard_cube_entry);
    standard_cube->transform = right_transform;
}

static void material_surface_on_init(void)
{
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;
    Mel_Material_Family_Handle surface = mel_material_family_find(S8("surface"));

    mel_render_list_init(&s_meshes,
        .name = S8("material_surface_meshes"),
        .entry_stride = sizeof(Mel_Mesh_Entry),
        .alloc = mel_alloc_heap());
    mel_render_list_init(&s_hud_text,
        .name = S8("material_surface_hud"),
        .entry_stride = sizeof(Mel_Text_Entry),
        .alloc = mel_alloc_heap());

    s_world_camera = (Mel_Camera){
        .view = mel_mat4_look_at(mel_vec3(0.0f, 1.2f, 8.0f), mel_vec3(0.0f, 0.0f, 0.0f), mel_vec3(0.0f, 1.0f, 0.0f)),
        .projection = mel_mat4_perspective(60.0f * (3.14159265f / 180.0f),
            (f32)sc->extent.width / (f32)sc->extent.height, 0.1f, 100.0f),
        .position = mel_vec3(0.0f, 1.2f, 8.0f),
    };
    s_overlay_camera = (Mel_Camera){
        .view = MEL_MAT4_IDENTITY,
        .projection = mel_mat4_ortho(0.0f, (f32)sc->extent.width, (f32)sc->extent.height, 0.0f, -1.0f, 1.0f),
    };

    s_font = mel_font_atlas_pool_load(mel_font_pool(),
        .path = S8("/System/Library/Fonts/Monaco.ttf"),
        .size = 20.0f);

    s_unlit_template = mel_material_template_create(&(Mel_Material_Template_Desc){
        .name = S8("demo_surface_unlit"),
        .family = surface,
        .profile = S8("surface.unlit"),
        .render_domain = MEL_MATERIAL_DOMAIN_OPAQUE,
        .fallback_policy = MEL_MATERIAL_FALLBACK_ALLOW,
        .base_color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
    });
    s_standard_template = mel_material_template_create(&(Mel_Material_Template_Desc){
        .name = S8("demo_surface_standard"),
        .family = surface,
        .profile = S8("surface.standard"),
        .render_domain = MEL_MATERIAL_DOMAIN_OPAQUE,
        .fallback_policy = MEL_MATERIAL_FALLBACK_ALLOW,
        .base_color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
        .params = (Mel_Material_Param_Desc[]){
            { .name = S8("roughness"), .type = MEL_MATERIAL_PARAM_F32, .f32_value = 0.35f },
            { .name = S8("metallic"), .type = MEL_MATERIAL_PARAM_F32, .f32_value = 0.15f },
            { .name = S8("occlusion"), .type = MEL_MATERIAL_PARAM_F32, .f32_value = 1.0f },
            { .name = S8("emissive"), .type = MEL_MATERIAL_PARAM_VEC4, .vec4_value = { .x = 0.08f, .y = 0.06f, .z = 0.02f, .w = 0.55f } },
        },
        .param_count = 4,
    });
    s_unlit_material = mel_material_instance_create(s_unlit_template);
    s_standard_material = mel_material_instance_create(s_standard_template);

    bool ok = mel_render_stage_3d_init(&s_stage,
        .name = S8("material_surface"),
        .swapchain = s_swapchain_handle,
        .world_camera = &s_world_camera,
        .hud_camera = &s_overlay_camera,
        .ui_camera = &s_overlay_camera,
        .clear_color_enabled = true,
        .clear_color = mel_vec4(s_clear_color[0], s_clear_color[1], s_clear_color[2], s_clear_color[3]),
        .enable_imgui = true,
        .imgui_fn = material_surface_imgui,
        .install_as_current_graph = true,
        .dev = mel_gpu_dev(),
        .mesh_pass = mel_mesh_pass(),
        .text_pass = mel_text_pass(),
        .alloc = mel_alloc_heap());
    assert(ok);

    mel_render_stage_3d_attach_mesh_list(&s_stage, &s_meshes);
    mel_render_stage_3d_attach_text_list_to_layer(&s_stage, MEL_RENDER_STAGE_3D_LAYER_HUD, &s_hud_text);

    s_unlit_cube_entry = mel_render_list_insert(&s_meshes, mel_sort_key_mesh_opaque(0.0f));
    Mel_Mesh_Entry* unlit_cube = mel_render_list_get(&s_meshes, s_unlit_cube_entry);
    *unlit_cube = (Mel_Mesh_Entry){
        .mesh = &s_cube_mesh,
        .transform = MEL_MAT4_IDENTITY,
        .color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
        .material = s_unlit_material,
    };

    s_standard_cube_entry = mel_render_list_insert(&s_meshes, mel_sort_key_mesh_opaque(0.1f));
    Mel_Mesh_Entry* standard_cube = mel_render_list_get(&s_meshes, s_standard_cube_entry);
    *standard_cube = (Mel_Mesh_Entry){
        .mesh = &s_cube_mesh,
        .transform = MEL_MAT4_IDENTITY,
        .color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
        .material = s_standard_material,
    };

    material_surface_extract(nullptr, 0.0f, nullptr);
    ok = mel_render_stage_3d_rebuild(&s_stage);
    assert(ok);

    mel_imgui_init(s_window_handle, &mel_swapchain_registry_get(s_swapchain_handle)->swapchain);
}

Mel_App_Config app_config(void)
{
    return (Mel_App_Config){
        .app_name = S8("Melody Surface Materials"),
        .enable_validation = true,
    };
}

void app_init(void)
{
    s_window_handle = mel_window_create(S8("Melody Surface Materials"), .width = WIN_W, .height = WIN_H);
    s_swapchain_handle = mel_gpu_swapchain_create_for_window_ex(mel_gpu_dev(), s_window_handle,
        .preferred_present_mode = VK_PRESENT_MODE_MAILBOX_KHR);
    mel_vfs_mount(S8("/"), mel_vfs_backend_os(), .root = S8("/"));

    material_surface_on_init();

    mel_sim_init(&s_sim, .event_buffer = s_event_buf, .event_buffer_size = sizeof(s_event_buf));
    mel_sim_add_variable(&s_sim, material_surface_update);
    mel_sim_add_variable(&s_sim, material_surface_extract);
    mel_register_sim(&s_sim);
}

void app_shutdown(void)
{
    mel_unregister_sim(&s_sim);
    mel_sim_shutdown(&s_sim);
    mel_render_stage_3d_shutdown(&s_stage);
    mel_material_instance_destroy(s_standard_material);
    mel_material_instance_destroy(s_unlit_material);
    mel_material_template_destroy(s_standard_template);
    mel_material_template_destroy(s_unlit_template);
    mel_render_list_shutdown(&s_hud_text);
    mel_render_list_shutdown(&s_meshes);
    mel_vfs_unmount(S8("/"));
}

void app_event(SDL_Event* event)
{
    mel_process_event(event);

    if (event->type == SDL_EVENT_QUIT)
    {
        mel_quit();
        return;
    }

    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE)
        mel_quit();
}
