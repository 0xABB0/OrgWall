#include <SDL3/SDL.h>

#include <cimgui/cimgui.h>

#include "core.app.h"
#include "core.engine.h"
#include "window.h"
#include "swapchain.h"
#include "gpu.swapchain.h"
#include "gpu.buffer.h"
#include "vfs.h"
#include "string.str8.h"
#include "render.stage.3d.h"
#include "render.frame_plan.h"
#include "render.view.h"
#include "render.list.h"
#include "render.camera.h"
#include "render.source.h"
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

typedef enum {
    MESH_VARIANT_LIST = 0,
    MESH_VARIANT_DRAW_STREAM,
    MESH_VARIANT_COUNT,
} Mesh_Variant_Mode;

typedef struct {
    f32 x, y, z;
    f32 r, g, b, a;
} Mesh_Stream_Vertex;

static Mel_Window_Handle s_window_handle;
static Mel_Swapchain_Handle s_swapchain_handle;
static Mel_Render_Stage_3D s_stage;
static Mel_Render_List s_mesh_list;
static Mel_Render_List s_hud_text;
static Mel_Camera s_world_camera;
static Mel_Camera s_overlay_camera;
static Mel_Sim_Ctx s_sim;
static u8 s_event_buf[4096];
static Mel_Font_Handle s_font;
static Mel_Source_Handle s_mesh_list_source;
static Mel_Source_Handle s_mesh_stream_source;
static Mel_Gpu_Buffer s_stream_vertex_buffer;
static Mel_Gpu_Buffer s_stream_index_buffer;
static Mel_Mesh_Gpu_Draw_Stream s_stream;
static u32 s_cube_entry;
static f32 s_time;
static f32 s_rotation_speed = 0.9f;
static f32 s_camera_distance = 5.5f;
static Mesh_Variant_Mode s_variant_mode = MESH_VARIANT_LIST;
static Mesh_Variant_Mode s_requested_variant_mode = MESH_VARIANT_LIST;
static const str8 s_game_mesh_variant_name = S8("game.mesh.accel");

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

static str8 mesh_variants_mode_label(Mesh_Variant_Mode mode)
{
    switch (mode)
    {
        case MESH_VARIANT_LIST: return S8("render list");
        case MESH_VARIANT_DRAW_STREAM: return S8("gpu draw stream");
        default: return S8("unknown");
    }
}

static Mel_Technique_Check_Result mesh_variants_support_game_accel(Mel_Frame_Plan_Technique_Ctx* ctx)
{
    Mel_Gpu_Capabilities caps = ctx->dev
        ? mel_gpu_capabilities(ctx->dev)
        : (Mel_Gpu_Capabilities){0};
    bool ok = caps.buffer_device_address && !caps.portability_subset;
    return (Mel_Technique_Check_Result){
        .ok = ok,
        .kind = ok ? MEL_TECHNIQUE_CHECK_OK : MEL_TECHNIQUE_CHECK_CAPABILITY_UNAVAILABLE,
        .reason = ok
            ? S8("game accel path enabled: device supports gpu-addressable buffers")
            : S8("game accel path disabled: requires non-portability gpu-addressable buffers"),
    };
}

static Mel_Technique_Check_Result mesh_variants_match_game_accel(Mel_Frame_Plan_Technique_Ctx* ctx)
{
    Mel_Source_Handle* read_sources = mel_frame_plan_collect_sources(ctx->plan,
        ctx->binding.view, MEL_SOURCE_GPU_BUFFER, MEL_SCHEMA_MESH_DRAW_STREAM);
    bool matched = read_sources != nullptr;
    mel_frame_plan_free_read_sources(ctx->plan, read_sources);
    return (Mel_Technique_Check_Result){
        .ok = matched,
        .kind = matched ? MEL_TECHNIQUE_CHECK_OK : MEL_TECHNIQUE_CHECK_SOURCE_MISMATCH,
        .reason = matched
            ? S8("game accel path found gpu draw-stream source")
            : S8("game accel path needs a gpu draw-stream source"),
    };
}

static Mel_Technique_Compile_Result mesh_variants_compile_game_accel(const Mel_Technique_Compile_Ctx* ctx)
{
    Mel_Mesh_Pass* mesh_pass = ctx->plan_ctx->opt.mesh_pass ? ctx->plan_ctx->opt.mesh_pass : mel_mesh_pass();
    Mel_Source_Handle* read_sources = mel_frame_plan_collect_sources(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.view, MEL_SOURCE_GPU_BUFFER, MEL_SCHEMA_MESH_DRAW_STREAM);
    if (!read_sources)
        return MEL_TECHNIQUE_COMPILE_SKIP;

    VkAttachmentLoadOp color_load_op = (!*ctx->plan_ctx->wrote_any_pass &&
        (ctx->plan_ctx->first_for_swapchain || ctx->plan_ctx->replace_contents))
        ? VK_ATTACHMENT_LOAD_OP_CLEAR
        : VK_ATTACHMENT_LOAD_OP_LOAD;
    Mel_Vec4 clear = mel_view_clear_color_enabled(ctx->plan_ctx->binding.view)
        ? mel_view_clear_color(ctx->plan_ctx->binding.view)
        : mel_vec4(0.0f, 0.0f, 0.0f, 1.0f);
    Mel_Render_Target* depth_target = mel_frame_plan_swapchain_depth_target(ctx->plan_ctx->plan,
        ctx->plan_ctx->binding.swapchain);
    bool ok = mel_frame_plan_add_graphics_pass(ctx->plan_ctx, ctx->technique->name,
        mel_mesh_pass_execute, mesh_pass, nullptr, read_sources, nullptr,
        MEL_WRITE_TARGETS(
            { .target = ctx->plan_ctx->target, .load_op = color_load_op,
              .clear.color = { .r = clear.x, .g = clear.y, .b = clear.z, .a = clear.w } },
            { .target = depth_target, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .clear.depth = { .depth = 1.0f, .stencil = 0 } }));
    mel_frame_plan_free_read_sources(ctx->plan_ctx->plan, read_sources);
    return ok ? MEL_TECHNIQUE_COMPILE_CONTRIBUTED : MEL_TECHNIQUE_COMPILE_FAIL;
}

static bool mesh_variants_refresh_game_accel(const Mel_Technique_Refresh_Ctx* ctx)
{
    return mel_frame_plan_refresh_contributed_passes(ctx->plan, ctx->view,
        ctx->pass_names, ctx->pass_count);
}

static void mesh_variants_register_custom_variants(void)
{
    mel_render_technique_register(&(Mel_Technique_Desc){
        .family = MEL_TECHNIQUE_MESH,
        .name = s_game_mesh_variant_name,
        .source_schema = MEL_SCHEMA_MESH_DRAW_STREAM,
        .priority = 300,
        .supports = mesh_variants_support_game_accel,
        .matches = mesh_variants_match_game_accel,
        .compile = mesh_variants_compile_game_accel,
        .refresh = mesh_variants_refresh_game_accel,
    });
}

static void mesh_variants_unregister_custom_variants(void)
{
    mel_render_technique_unregister(MEL_TECHNIQUE_MESH, s_game_mesh_variant_name);
}

static bool mesh_variants_same_source(Mel_Source_Handle a, Mel_Source_Handle b)
{
    return a.handle.index == b.handle.index &&
        a.handle.generation == b.handle.generation;
}

static void mesh_variants_sync_viewport(void)
{
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;
    i32 w = 0;
    i32 h = 0;
    mel_window_size_pixels(s_window_handle, &w, &h);
    if (w <= 0 || h <= 0)
        return;

    s_overlay_camera.view = MEL_MAT4_IDENTITY;
    s_overlay_camera.projection = mel_mat4_ortho(0.0f, (f32)w, 0.0f, (f32)h, -1.0f, 1.0f);

    if (sc->extent.width != (u32)w || sc->extent.height != (u32)h)
    {
        mel_swapchain_resize(sc, mel_gpu_dev(), (u32)w, (u32)h);
        s_world_camera.projection = mel_mat4_perspective(60.0f * (3.14159265f / 180.0f),
            (f32)w / (f32)h, 0.1f, 100.0f);
    }

    bool ok = mel_render_stage_3d_refresh(&s_stage);
    assert(ok);
    MEL_UNUSED(ok);
}

static void mesh_variants_upload_stream(Mel_Mat4 transform)
{
    Mesh_Stream_Vertex vertices[SDL_arraysize(s_cube_positions)];
    for (u32 i = 0; i < SDL_arraysize(s_cube_positions); i++)
    {
        Mel_Vec3 p = mel_mat4_mul_point(transform, s_cube_positions[i]);
        Mel_Vec4 c = s_cube_colors[i];
        vertices[i] = (Mesh_Stream_Vertex){
            .x = p.x,
            .y = p.y,
            .z = p.z,
            .r = c.x,
            .g = c.y,
            .b = c.z,
            .a = c.w,
        };
    }

    mel_gpu_buffer_upload(&s_stream_vertex_buffer, mel_gpu_dev(), vertices, sizeof(vertices), 0);
}

static str8 mesh_variants_resolved_name(void)
{
    Mel_Frame_Plan_Handle plan = mel_render_stage_3d_plan(&s_stage);
    Mel_View_Handle world_view = mel_render_stage_3d_view(&s_stage, MEL_RENDER_STAGE_3D_LAYER_WORLD);
    u32 count = mel_frame_plan_resolved_technique_count(plan);
    for (u32 i = 0; i < count; i++)
    {
        Mel_Frame_Plan_Resolved_Technique resolved = {0};
        if (!mel_frame_plan_resolved_technique_at(plan, i, &resolved))
            continue;
        if (resolved.family != MEL_TECHNIQUE_MESH)
            continue;
        if (resolved.view.handle.index != world_view.handle.index ||
            resolved.view.handle.generation != world_view.handle.generation)
            continue;
        return resolved.technique_name;
    }

    return S8("unresolved");
}

static u32 mesh_variants_diagnostic_lines(char lines[][160], u32 max_lines)
{
    for (u32 i = 0; i < max_lines; i++)
        lines[i][0] = '\0';

    Mel_Frame_Plan_Handle plan = mel_render_stage_3d_plan(&s_stage);
    Mel_View_Handle world_view = mel_render_stage_3d_view(&s_stage, MEL_RENDER_STAGE_3D_LAYER_WORLD);
    u32 count = mel_frame_plan_technique_diagnostic_count(plan);
    u32 found = 0;

    for (u32 i = 0; i < count && found < max_lines; i++)
    {
        Mel_Frame_Plan_Technique_Diagnostic diag = {0};
        if (!mel_frame_plan_technique_diagnostic_at(plan, i, &diag))
            continue;
        if (diag.family != MEL_TECHNIQUE_MESH)
            continue;
        if (diag.view.handle.index != world_view.handle.index ||
            diag.view.handle.generation != world_view.handle.generation)
            continue;

        SDL_snprintf(lines[found], 160, "%s%.*s: %.*s",
            diag.selected ? "* " : "  ",
            (int)diag.technique_name.len, diag.technique_name.data,
            (int)diag.reason.len, diag.reason.data);
        found++;
    }

    return found;
}

static void mesh_variants_apply_mode(Mesh_Variant_Mode mode)
{
    Mel_View_Handle world_view = mel_render_stage_3d_view(&s_stage, MEL_RENDER_STAGE_3D_LAYER_WORLD);
    mel_view_detach_source(world_view, s_mesh_list_source);
    mel_view_detach_source(world_view, s_mesh_stream_source);

    Mel_Source_Handle source = mode == MESH_VARIANT_DRAW_STREAM
        ? s_mesh_stream_source
        : s_mesh_list_source;
    bool ok = mel_render_stage_3d_attach_mesh_source_to_layer(&s_stage,
        MEL_RENDER_STAGE_3D_LAYER_WORLD, source);
    assert(ok);

    ok = mel_render_stage_3d_rebuild(&s_stage);
    assert(ok);
    s_variant_mode = mode;
    s_requested_variant_mode = mode;
}

static void mesh_variants_request_mode(Mesh_Variant_Mode mode)
{
    s_requested_variant_mode = mode;
}

static void mesh_variants_imgui(void* user)
{
    MEL_UNUSED(user);

    Mel_Frame_Stats frame = mel_frame_stats();
    Mel_Gpu_Capabilities caps = mel_gpu_capabilities(mel_gpu_dev());
    str8 resolved = mesh_variants_resolved_name();
    char diag_lines[4][160];
    u32 diag_count = mesh_variants_diagnostic_lines(diag_lines, SDL_arraysize(diag_lines));
    int mode = (int)s_variant_mode;

    igSetNextWindowPos((ImVec2){ 24.0f, 24.0f }, ImGuiCond_FirstUseEver, (ImVec2){0});
    igSetNextWindowSize((ImVec2){ 420.0f, 300.0f }, ImGuiCond_FirstUseEver);
    if (igBegin("Mesh Variants", nullptr, 0))
    {
        igText("One world view, one mesh family, game-extensible variants.");
        igSeparator();
        igText("Resolved technique: %.*s", (int)resolved.len, resolved.data);
        igText("Requested family: mesh");
        igText("Frame: %.2f ms (%.1f fps)", frame.dt * 1000.0f, frame.fps);
        igText("Caps: mesh_shader=%s descriptor_buffer=%s portability=%s",
            caps.mesh_shader ? "yes" : "no",
            caps.descriptor_buffer ? "yes" : "no",
            caps.portability_subset ? "yes" : "no");
        igSeparator();
        if (igRadioButton_IntPtr("Render List", &mode, MESH_VARIANT_LIST))
            mesh_variants_request_mode(MESH_VARIANT_LIST);
        if (igRadioButton_IntPtr("GPU Draw Stream", &mode, MESH_VARIANT_DRAW_STREAM))
            mesh_variants_request_mode(MESH_VARIANT_DRAW_STREAM);
        igSliderFloat("Rotation Speed", &s_rotation_speed, 0.0f, 2.0f, "%.2f", 0);
        igSliderFloat("Camera Distance", &s_camera_distance, 3.5f, 9.0f, "%.2f", 0);
        igSeparator();
        igText("Resolution trace:");
        for (u32 i = 0; i < diag_count; i++)
            igTextWrapped("%s", diag_lines[i]);
        igTextWrapped("Press 1 or 2 to swap input style. The game also registers a higher-priority mesh variant and lets device capability checks decide whether it can win.");
    }
    igEnd();
}

static void mesh_variants_extract(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(dt);
    MEL_UNUSED(user);

    mel_render_list_clear(&s_hud_text);

    str8 mode = mesh_variants_mode_label(s_variant_mode);
    str8 resolved = mesh_variants_resolved_name();
    Mel_Gpu_Capabilities caps = mel_gpu_capabilities(mel_gpu_dev());
    char line[128];
    char caps_line[128];
    char diag_lines[4][160];
    u32 diag_count = mesh_variants_diagnostic_lines(diag_lines, SDL_arraysize(diag_lines));
    SDL_snprintf(line, sizeof(line), "Mode: %.*s  ->  %.*s",
        (int)mode.len, mode.data, (int)resolved.len, resolved.data);
    SDL_snprintf(caps_line, sizeof(caps_line), "Caps: mesh_shader=%s descriptor_buffer=%s portability=%s",
        caps.mesh_shader ? "yes" : "no",
        caps.descriptor_buffer ? "yes" : "no",
        caps.portability_subset ? "yes" : "no");

    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, S8("Mesh Technique Variant Demo"),
        .x = 28.0f,
        .y = 740.0f,
        .style = { .color = mel_vec4(0.95f, 0.96f, 0.98f, 1.0f) });
    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, str8_from_cstr(line),
        .x = 28.0f,
        .y = 708.0f,
        .style = { .color = mel_vec4(0.75f, 0.86f, 0.96f, 1.0f) });
    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text,
        S8("Same world view and family request. The game adds its own variant and the engine resolves it against device capabilities."),
        .x = 28.0f,
        .y = 676.0f,
        .style = { .color = mel_vec4(0.78f, 0.80f, 0.84f, 1.0f) });
    mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, str8_from_cstr(caps_line),
        .x = 28.0f,
        .y = 648.0f,
        .style = { .color = mel_vec4(0.76f, 0.84f, 0.92f, 1.0f) });
    for (u32 i = 0; i < diag_count; i++)
    {
        mel_text_draw_font_atlas(mel_font_pool(), s_font, &s_hud_text, str8_from_cstr(diag_lines[i]),
            .x = 28.0f,
            .y = 616.0f - (f32)(i * 28),
            .style = { .color = i == 0
                ? mel_vec4(0.82f, 0.86f, 0.90f, 1.0f)
                : mel_vec4(0.70f, 0.74f, 0.78f, 1.0f) });
    }
}

static void mesh_variants_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(user);

    if (s_requested_variant_mode != s_variant_mode)
        mesh_variants_apply_mode(s_requested_variant_mode);

    s_time += dt;
    mesh_variants_sync_viewport();

    f32 angle = s_time * s_rotation_speed;
    s_world_camera.view = mel_mat4_look_at(
        mel_vec3(cosf(angle * 0.35f) * s_camera_distance, 2.4f, sinf(angle * 0.35f) * s_camera_distance),
        mel_vec3(0.0f, 0.0f, 0.0f),
        mel_vec3(0.0f, 1.0f, 0.0f));
    s_world_camera.position = mel_vec3(0.0f, 2.4f, s_camera_distance);

    Mel_Mat4 rotation = mel_mat4_rotateXYZ(angle * 0.85f, angle * 1.15f, angle * 0.30f);
    Mel_Mat4 scale = mel_mat4_scalef(0.95f);
    Mel_Mat4 transform = mel_mat4_mul(rotation, scale);

    Mel_Mesh_Entry* cube = mel_render_list_get(&s_mesh_list, s_cube_entry);
    cube->transform = transform;
    mesh_variants_upload_stream(transform);
}

static void mesh_variants_on_init(void)
{
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;

    mesh_variants_register_custom_variants();

    mel_render_list_init(&s_mesh_list,
        .name = S8("mesh_variant_world"),
        .entry_stride = sizeof(Mel_Mesh_Entry),
        .alloc = mel_alloc_heap());
    mel_render_list_init(&s_hud_text,
        .name = S8("mesh_variant_hud"),
        .entry_stride = sizeof(Mel_Text_Entry),
        .alloc = mel_alloc_heap());

    s_world_camera = (Mel_Camera){
        .projection = mel_mat4_perspective(60.0f * (3.14159265f / 180.0f),
            (f32)sc->extent.width / (f32)sc->extent.height, 0.1f, 100.0f),
    };
    s_overlay_camera = (Mel_Camera){
        .view = MEL_MAT4_IDENTITY,
        .projection = mel_mat4_ortho(0.0f, (f32)sc->extent.width, 0.0f, (f32)sc->extent.height, -1.0f, 1.0f),
    };

    s_font = mel_font_atlas_pool_load(mel_font_pool(),
        .path = S8("/System/Library/Fonts/Monaco.ttf"),
        .size = 20.0f);

    bool ok = mel_render_stage_3d_init(&s_stage,
        .name = S8("mesh_variants"),
        .swapchain = s_swapchain_handle,
        .world_camera = &s_world_camera,
        .hud_camera = &s_overlay_camera,
        .ui_camera = &s_overlay_camera,
        .clear_color_enabled = true,
        .clear_color = mel_vec4(0.07f, 0.08f, 0.11f, 1.0f),
        .enable_imgui = true,
        .imgui_fn = mesh_variants_imgui,
        .install_as_current_graph = true,
        .dev = mel_gpu_dev(),
        .mesh_pass = mel_mesh_pass(),
        .sprite_pass = mel_sprite_pass(),
        .text_pass = mel_text_pass(),
        .alloc = mel_alloc_heap());
    assert(ok);

    mel_render_stage_3d_attach_text_list_to_layer(&s_stage, MEL_RENDER_STAGE_3D_LAYER_HUD, &s_hud_text);

    s_cube_entry = mel_render_list_insert(&s_mesh_list, mel_sort_key_mesh_opaque(0.0f));
    Mel_Mesh_Entry* cube = mel_render_list_get(&s_mesh_list, s_cube_entry);
    *cube = (Mel_Mesh_Entry){
        .mesh = &s_cube_mesh,
        .transform = MEL_MAT4_IDENTITY,
        .color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
    };

    mel_gpu_buffer_init(&s_stream_vertex_buffer, mel_gpu_dev(),
        .size = sizeof(Mesh_Stream_Vertex) * SDL_arraysize(s_cube_positions),
        .usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        .memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU);
    mel_gpu_buffer_init(&s_stream_index_buffer, mel_gpu_dev(),
        .size = sizeof(s_cube_indices),
        .usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        .memory_usage = VMA_MEMORY_USAGE_CPU_TO_GPU);
    mel_gpu_buffer_upload(&s_stream_index_buffer, mel_gpu_dev(), s_cube_indices, sizeof(s_cube_indices), 0);

    s_stream = (Mel_Mesh_Gpu_Draw_Stream){
        .vertex_buffer = s_stream_vertex_buffer.buffer,
        .index_buffer = s_stream_index_buffer.buffer,
        .index_count = SDL_arraysize(s_cube_indices),
    };

    s_mesh_list_source = mel_source_from_render_list(&s_mesh_list, MEL_SCHEMA_MESH_INSTANCE);
    s_mesh_stream_source = mel_source_create(&(Mel_Source_Desc){
        .name = S8("mesh_draw_stream"),
        .kind = MEL_SOURCE_GPU_BUFFER,
        .schema = MEL_SCHEMA_MESH_DRAW_STREAM,
        .access_flags = MEL_SOURCE_ACCESS_CPU_WRITE | MEL_SOURCE_ACCESS_GPU_READ,
        .lifetime = MEL_SOURCE_LIFETIME_EXTERNAL,
        .user = &s_stream,
    });
    mel_source_set_gpu_buffer(s_mesh_stream_source, &s_stream_vertex_buffer);

    mesh_variants_upload_stream(MEL_MAT4_IDENTITY);
    mesh_variants_apply_mode(MESH_VARIANT_LIST);
    mesh_variants_extract(nullptr, 0.0f, nullptr);

    mel_imgui_init(s_window_handle, &mel_swapchain_registry_get(s_swapchain_handle)->swapchain);
}

void app_init(void)
{
    s_window_handle = mel_window_create(S8("Melody Mesh Variants"), .width = WIN_W, .height = WIN_H);
    s_swapchain_handle = mel_gpu_swapchain_create_for_window_ex(mel_gpu_dev(), s_window_handle,
        .preferred_present_mode = VK_PRESENT_MODE_MAILBOX_KHR);
    mel_vfs_mount_native(mel_vfs(), S8("/"), S8("/"), 0, false);

    mesh_variants_on_init();

    mel_sim_init(&s_sim, .event_buffer = s_event_buf, .event_buffer_size = sizeof(s_event_buf));
    mel_sim_add_variable(&s_sim, mesh_variants_update);
    mel_sim_add_variable(&s_sim, mesh_variants_extract);
    mel_register_sim(&s_sim);
}

Mel_App_Config app_config(void)
{
    return (Mel_App_Config){
        .app_name = S8("Melody Mesh Variants"),
        .enable_validation = true,
    };
}

void app_shutdown(void)
{
    mel_unregister_sim(&s_sim);
    mel_sim_shutdown(&s_sim);

    mel_render_stage_3d_shutdown(&s_stage);
    mesh_variants_unregister_custom_variants();
    mel_render_list_shutdown(&s_hud_text);
    mel_render_list_shutdown(&s_mesh_list);
    mel_source_destroy(s_mesh_stream_source);
    mel_source_destroy(s_mesh_list_source);
    mel_gpu_buffer_shutdown(&s_stream_index_buffer, mel_gpu_dev());
    mel_gpu_buffer_shutdown(&s_stream_vertex_buffer, mel_gpu_dev());
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
            mesh_variants_request_mode(MESH_VARIANT_LIST);
            break;
        case SDL_SCANCODE_2:
            mesh_variants_request_mode(MESH_VARIANT_DRAW_STREAM);
            break;
        default:
            break;
    }
}
