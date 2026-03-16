#include <SDL3/SDL.h>
#include <math.h>

#include "core.app.h"
#include "core.engine.h"
#include "window.h"
#include "swapchain.h"
#include "gpu.swapchain.h"
#include "string.str8.h"
#include "render.stage.3d.h"
#include "render.list.h"
#include "render.camera.h"
#include "render.camera.orbit.h"
#include "render.grid.h"
#include "render.material.h"
#include "mesh.pass.h"
#include "allocator.heap.h"
#include "math.mat4.h"
#include "math.vec4.h"
#include "math.vec3.h"
#include "math.scalar.h"
#include "sim.ctx.h"
#include "sim.verlet.h"

#define WIN_W 1280
#define WIN_H 720

#define CLOTH_W 20
#define CLOTH_H 20
#define CLOTH_SPACING 0.15f

#define SPHERE_RINGS 16
#define SPHERE_SECTORS 24
#define SPHERE_VERTS ((SPHERE_RINGS + 1) * (SPHERE_SECTORS + 1))
#define SPHERE_INDICES (SPHERE_RINGS * SPHERE_SECTORS * 6)

static Mel_Window_Handle s_window;
static Mel_Swapchain_Handle s_swapchain;
static Mel_Render_Stage_3D s_stage;
static Mel_Render_List s_mesh_list;
static Mel_Camera s_camera;
static Mel_Orbit_Camera s_orbit;
static Mel_Sim_Ctx s_sim;
static Mel_Sim_Fixed* s_fixed;
static u8 s_event_buf[4096];
static f32 s_time;

static Mel_Grid s_grid;
static Mel_Verlet_System s_cloth;
static u32 s_sphere_collider;
static Mel_Vec3 s_sphere_pos;
static f32 s_sphere_radius;

static Mel_Vec3 s_cloth_positions[CLOTH_W * CLOTH_H];
static Mel_Vec3 s_cloth_normals[CLOTH_W * CLOTH_H];
static Mel_Vec4 s_cloth_colors[CLOTH_W * CLOTH_H];
static u32 s_cloth_indices[(CLOTH_W - 1) * (CLOTH_H - 1) * 6];
static Mel_Mesh s_cloth_mesh;

static Mel_Vec3 s_sphere_positions[SPHERE_VERTS];
static Mel_Vec3 s_sphere_normals[SPHERE_VERTS];
static Mel_Vec4 s_sphere_colors[SPHERE_VERTS];
static u32 s_sphere_indices[SPHERE_INDICES];
static Mel_Mesh s_sphere_mesh;

static Mel_Material_Template_Handle s_lit_template;
static Mel_Material_Instance_Handle s_lit_material;
static Mel_Material_Template_Handle s_grid_template;
static Mel_Material_Instance_Handle s_grid_material;

static void build_sphere_mesh(Mel_Vec3 center, f32 radius, Mel_Vec4 color)
{
    u32 vi = 0;
    for (u32 r = 0; r <= SPHERE_RINGS; r++) {
        f32 phi = MEL_PI * (f32)r / (f32)SPHERE_RINGS;
        f32 sp = sinf(phi);
        f32 cp = cosf(phi);
        for (u32 s = 0; s <= SPHERE_SECTORS; s++) {
            f32 theta = MEL_TAU * (f32)s / (f32)SPHERE_SECTORS;
            Mel_Vec3 n = mel_vec3(sp * cosf(theta), cp, sp * sinf(theta));
            s_sphere_normals[vi] = n;
            s_sphere_positions[vi] = mel_vec3(
                center.x + radius * n.x,
                center.y + radius * n.y,
                center.z + radius * n.z);
            s_sphere_colors[vi] = color;
            vi++;
        }
    }

    u32 ii = 0;
    for (u32 r = 0; r < SPHERE_RINGS; r++) {
        for (u32 s = 0; s < SPHERE_SECTORS; s++) {
            u32 a = r * (SPHERE_SECTORS + 1) + s;
            u32 b = a + SPHERE_SECTORS + 1;
            s_sphere_indices[ii++] = a;
            s_sphere_indices[ii++] = b;
            s_sphere_indices[ii++] = a + 1;
            s_sphere_indices[ii++] = a + 1;
            s_sphere_indices[ii++] = b;
            s_sphere_indices[ii++] = b + 1;
        }
    }

    s_sphere_mesh = (Mel_Mesh){
        .positions = s_sphere_positions,
        .normals = s_sphere_normals,
        .colors = s_sphere_colors,
        .vertex_count = SPHERE_VERTS,
        .indices = s_sphere_indices,
        .index_count = SPHERE_INDICES,
    };
}

static void update_sphere_positions(Mel_Vec3 center, f32 radius)
{
    u32 vi = 0;
    for (u32 r = 0; r <= SPHERE_RINGS; r++) {
        f32 phi = MEL_PI * (f32)r / (f32)SPHERE_RINGS;
        f32 sp = sinf(phi);
        f32 cp = cosf(phi);
        for (u32 s = 0; s <= SPHERE_SECTORS; s++) {
            f32 theta = MEL_TAU * (f32)s / (f32)SPHERE_SECTORS;
            Mel_Vec3 n = mel_vec3(sp * cosf(theta), cp, sp * sinf(theta));
            s_sphere_positions[vi++] = mel_vec3(
                center.x + radius * n.x,
                center.y + radius * n.y,
                center.z + radius * n.z);
        }
    }
}

static void sync_viewport(void)
{
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain)->swapchain;
    i32 w = 0, h = 0;
    mel_window_size_pixels(s_window, &w, &h);
    if (w <= 0 || h <= 0) return;

    if (sc->extent.width != (u32)w || sc->extent.height != (u32)h) {
        mel_swapchain_resize(sc, mel_gpu_dev(), (u32)w, (u32)h);
        mel_orbit_camera_update(&s_orbit, &s_camera, (f32)w / (f32)h);
        mel_render_stage_3d_refresh(&s_stage);
    }
}

static void physics_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(user);

    mel_verlet_apply_force(&s_cloth, mel_vec3(0, -9.8f, 0));

    f32 wx = 2.0f * sinf(s_time * 0.8f);
    f32 wz = 1.5f * cosf(s_time * 0.5f);
    mel_verlet_apply_wind(&s_cloth, mel_vec3(wx, 0, wz));

    mel_verlet_step(&s_cloth, dt);
}

static void render_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(user);

    s_time += dt;
    sync_viewport();

    f32 bob = sinf(s_time * 0.5f) * 0.15f;
    s_sphere_pos = mel_vec3(0.0f, 0.8f + bob, 0.0f);
    mel_verlet_update_collider_sphere(&s_cloth, s_sphere_collider,
        mel_sphere(s_sphere_pos, s_sphere_radius));
    update_sphere_positions(s_sphere_pos, s_sphere_radius);

    mel_verlet_compute_normals(&s_cloth, CLOTH_W, CLOTH_H);
    mel_verlet_cloth_mesh(&s_cloth, CLOTH_W, CLOTH_H,
        s_cloth_positions, s_cloth_normals, NULL, s_cloth_colors, s_cloth_indices,
        (Mel_Vec4){{0.85f, 0.25f, 0.55f, 1.0f}});

    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain)->swapchain;
    mel_orbit_camera_update(&s_orbit, &s_camera,
        (f32)sc->extent.width / (f32)sc->extent.height);
}

static void setup_scene(void)
{
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain)->swapchain;

    mel_render_list_init(&s_mesh_list,
        .name = S8("cloth_meshes"),
        .entry_stride = sizeof(Mel_Mesh_Entry),
        .alloc = mel_alloc_heap());

    mel_orbit_camera_init(&s_orbit,
        .target = mel_vec3(0, 0.8f, 0),
        .distance = 5.0f,
        .yaw = 0.6f,
        .pitch = 0.5f);

    mel_orbit_camera_update(&s_orbit, &s_camera,
        (f32)sc->extent.width / (f32)sc->extent.height);

    mel_render_stage_3d_init(&s_stage,
        .name = S8("cloth_demo"),
        .swapchain = s_swapchain,
        .world_camera = &s_camera,
        .clear_color_enabled = true,
        .clear_color = mel_vec4(0.08f, 0.08f, 0.10f, 1.0f),
        .install_as_current_graph = true,
        .dev = mel_gpu_dev(),
        .mesh_pass = mel_mesh_pass(),
        .alloc = mel_alloc_heap());

    mel_mesh_pass_set_lighting(mel_mesh_pass(), (Mel_Mesh_Lighting){
        .direction = mel_vec3(-0.3f, -0.8f, -0.4f),
        .color = mel_vec4(1.0f, 0.95f, 0.9f, 1.0f),
        .ambient = 0.2f,
    });

    mel_render_stage_3d_attach_mesh_list(&s_stage, &s_mesh_list);
    mel_render_stage_3d_rebuild(&s_stage);

    Mel_Material_Family_Handle surface_family = mel_material_family_find(S8("surface"));
    s_lit_template = mel_material_template_create(&(Mel_Material_Template_Desc){
        .name = S8("cloth_lit"),
        .family = surface_family,
        .profile = S8("surface.standard"),
        .render_domain = MEL_MATERIAL_DOMAIN_OPAQUE,
        .fallback_policy = MEL_MATERIAL_FALLBACK_ALLOW,
        .cull_mode = MEL_MATERIAL_CULL_NONE,
        .base_color = mel_vec4(1, 1, 1, 1),
        .params = (Mel_Material_Param_Desc[]){
            { .name = S8("roughness"), .type = MEL_MATERIAL_PARAM_F32, .f32_value = 0.6f },
            { .name = S8("metallic"), .type = MEL_MATERIAL_PARAM_F32, .f32_value = 0.0f },
        },
        .param_count = 2,
    });
    s_lit_material = mel_material_instance_create(s_lit_template);

    s_grid_template = mel_material_template_create(&(Mel_Material_Template_Desc){
        .name = S8("grid"),
        .family = surface_family,
        .profile = S8("surface.standard"),
        .render_domain = MEL_MATERIAL_DOMAIN_OPAQUE,
        .cull_mode = MEL_MATERIAL_CULL_NONE,
        .base_color = mel_vec4(1, 1, 1, 1),
    });
    s_grid_material = mel_material_instance_create(s_grid_template);

    mel_grid_init(&s_grid, .y = 0.0f);

    mel_verlet_init(&s_cloth,
        .alloc = mel_alloc_heap());

    f32 half_w = (f32)(CLOTH_W - 1) * CLOTH_SPACING * 0.5f;
    f32 half_h = (f32)(CLOTH_H - 1) * CLOTH_SPACING * 0.5f;
    mel_verlet_cloth(&s_cloth,
        .width = CLOTH_W,
        .height = CLOTH_H,
        .spacing = CLOTH_SPACING,
        .origin = mel_vec3(-half_w, 1.5f, -half_h),
        .pin_top_row = true,
        .add_shear = true);

    s_sphere_pos = mel_vec3(0, 0.8f, 0);
    s_sphere_radius = 0.5f;
    s_sphere_collider = mel_verlet_add_collider_sphere(&s_cloth,
        mel_sphere(s_sphere_pos, s_sphere_radius));

    mel_verlet_add_collider_plane(&s_cloth,
        mel_plane_from_normal_point(mel_vec3(0, 1, 0), mel_vec3(0, 0.0f, 0)));

    build_sphere_mesh(s_sphere_pos, s_sphere_radius,
        (Mel_Vec4){{0.4f, 0.45f, 0.5f, 1.0f}});

    Mel_Verlet_Mesh_Counts counts = mel_verlet_cloth_mesh_counts(CLOTH_W, CLOTH_H);
    s_cloth_mesh = (Mel_Mesh){
        .positions = s_cloth_positions,
        .normals = s_cloth_normals,
        .colors = s_cloth_colors,
        .vertex_count = counts.vertex_count,
        .indices = s_cloth_indices,
        .index_count = counts.index_count,
    };

    mel_verlet_compute_normals(&s_cloth, CLOTH_W, CLOTH_H);
    mel_verlet_cloth_mesh(&s_cloth, CLOTH_W, CLOTH_H,
        s_cloth_positions, s_cloth_normals, NULL, s_cloth_colors, s_cloth_indices,
        (Mel_Vec4){{0.85f, 0.25f, 0.55f, 1.0f}});

    u32 grid_entry = mel_render_list_insert(&s_mesh_list, mel_sort_key_mesh_opaque(100.0f));
    Mel_Mesh_Entry* ge = mel_render_list_get(&s_mesh_list, grid_entry);
    *ge = (Mel_Mesh_Entry){
        .mesh = &s_grid.mesh,
        .transform = MEL_MAT4_IDENTITY,
        .color = mel_vec4(1, 1, 1, 1),
        .material = s_grid_material,
    };

    u32 cloth_entry = mel_render_list_insert(&s_mesh_list, mel_sort_key_mesh_opaque(0.0f));
    Mel_Mesh_Entry* ce = mel_render_list_get(&s_mesh_list, cloth_entry);
    *ce = (Mel_Mesh_Entry){
        .mesh = &s_cloth_mesh,
        .transform = MEL_MAT4_IDENTITY,
        .color = mel_vec4(1, 1, 1, 1),
        .material = s_lit_material,
    };

    u32 sphere_entry = mel_render_list_insert(&s_mesh_list, mel_sort_key_mesh_opaque(1.0f));
    Mel_Mesh_Entry* se = mel_render_list_get(&s_mesh_list, sphere_entry);
    *se = (Mel_Mesh_Entry){
        .mesh = &s_sphere_mesh,
        .transform = MEL_MAT4_IDENTITY,
        .color = mel_vec4(1, 1, 1, 1),
        .material = s_lit_material,
    };
}

void app_init(void)
{
    s_window = mel_window_create(S8("Melody Cloth Simulation"),
        .width = WIN_W, .height = WIN_H);
    s_swapchain = mel_gpu_swapchain_create_for_window(mel_gpu_dev(), s_window);

    setup_scene();

    mel_sim_init(&s_sim,
        .event_buffer = s_event_buf,
        .event_buffer_size = sizeof(s_event_buf));

    s_fixed = mel_sim_add_fixed(&s_sim, .fixed_dt = 1.0f / 60.0f);
    mel_sim_fixed_add_update(s_fixed, physics_update);
    mel_sim_add_variable(&s_sim, render_update);
    mel_register_sim(&s_sim);
}

void app_shutdown(void)
{
    mel_unregister_sim(&s_sim);
    mel_sim_shutdown(&s_sim);
    mel_verlet_shutdown(&s_cloth);
    mel_material_instance_destroy(s_grid_material);
    mel_material_template_destroy(s_grid_template);
    mel_material_instance_destroy(s_lit_material);
    mel_material_template_destroy(s_lit_template);
    mel_render_stage_3d_shutdown(&s_stage);
    mel_render_list_shutdown(&s_mesh_list);
}

void app_event(SDL_Event* event)
{
    mel_orbit_camera_event(&s_orbit, event);

    if (event->type == SDL_EVENT_QUIT)
        mel_quit();
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.key == SDLK_ESCAPE)
        mel_quit();
}
