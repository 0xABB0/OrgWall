#include <math.h>
#include <stddef.h>

#include <allocator/allocator.h>
#include <allocator/heap.h>
#include <boot/boot.h>
#include <gpu.h>
#include <gui/gui.h>
#include <log/log.h>
#include <string/str8.h>
#include <vat/vat.h>

#include "ballgame_bundle.h"
#include "bundle_select.h"

#define BALL_RADIUS    24.0f
#define BALL_MAX_SPEED 680.0f
#define BALL_RESPONSE  8.0f
#define BALL_BOUNCE    0.6f
#define DRAG_ARRIVE    8.0f
#define TRAIL_LEN      24
#define VBO_FRAMES     3
#define SPRITE_VERTS   6
#define VERT_CAP       ((1 + TRAIL_LEN + 3) * SPRITE_VERTS)

typedef struct
{
    f32 pos[2];
    f32 uv[3];
    f32 color[4];
} Ball_Vertex;

typedef struct
{
    Mel_Vat*               vat;
    Mel_Gpu_Instance*      instance;
    Mel_Gpu_Device*        device;
    Mel_Gui_Handle         view;
    Mel_Gpu_Surface*       surface;
    Mel_Gpu_Swapchain*     swapchain;
    Mel_Gpu_Render_Source* source;
    Mel_Gpu_Shader         shader;
    Mel_Gpu_Pipeline       pipeline;
    Mel_Gpu_Buffer         vbo[VBO_FRAMES];
    i32                    frame;
    bool                   gpu_ready;
    bool                   scene_ready;
    Ball_Vertex*           verts;
    f32*                   trail;
    i32                    trail_count;
    f32                    x, y, vx, vy;
    f32                    view_w, view_h;
    bool                   up, left, down, right;
    bool                   dragging;
    f32                    drag_x, drag_y;
    bool                   placed;
} Ball_Game;

static Ball_Game g;

static void key_state(Mel_Key key, bool held)
{
    if (key == MEL_KEY_W || key == MEL_KEY_UP)
        g.up = held;
    if (key == MEL_KEY_A || key == MEL_KEY_LEFT)
        g.left = held;
    if (key == MEL_KEY_S || key == MEL_KEY_DOWN)
        g.down = held;
    if (key == MEL_KEY_D || key == MEL_KEY_RIGHT)
        g.right = held;
}

static void on_key_down(Mel_Gui_Handle h, Mel_Key key, void* user)
{
    (void)h;
    (void)user;
    key_state(key, true);
}

static void on_key_up(Mel_Gui_Handle h, Mel_Key key, void* user)
{
    (void)h;
    (void)user;
    key_state(key, false);
}

static void on_pointer_down(Mel_Gui_Handle h, i32 x, i32 y, void* user)
{
    (void)h;
    (void)user;
    g.dragging = true;
    g.drag_x = (f32)x;
    g.drag_y = (f32)y;
}

static void on_pointer_move(Mel_Gui_Handle h, i32 x, i32 y, void* user)
{
    (void)h;
    (void)user;
    if (!g.dragging)
        return;
    g.drag_x = (f32)x;
    g.drag_y = (f32)y;
}

static void on_pointer_up(Mel_Gui_Handle h, i32 x, i32 y, void* user)
{
    (void)h;
    (void)x;
    (void)y;
    (void)user;
    g.dragging = false;
}

static void simulate(f32 dt)
{
    if (!g.placed || g.view_w <= 0 || g.view_h <= 0)
        return;

    f32 want_x = 0, want_y = 0;
    if (g.dragging)
    {
        f32 dx = g.drag_x - g.x;
        f32 dy = g.drag_y - g.y;
        f32 dist = sqrtf(dx * dx + dy * dy);
        if (dist > 1.0f)
        {
            f32 speed = dist * DRAG_ARRIVE;
            if (speed > BALL_MAX_SPEED)
                speed = BALL_MAX_SPEED;
            want_x = dx / dist * speed;
            want_y = dy / dist * speed;
        }
    }
    else
    {
        f32 dx = (f32)((g.right ? 1 : 0) - (g.left ? 1 : 0));
        f32 dy = (f32)((g.down ? 1 : 0) - (g.up ? 1 : 0));
        f32 len = sqrtf(dx * dx + dy * dy);
        if (len > 0.0f)
        {
            want_x = dx / len * BALL_MAX_SPEED;
            want_y = dy / len * BALL_MAX_SPEED;
        }
    }

    f32 blend = 1.0f - expf(-BALL_RESPONSE * dt);
    g.vx += (want_x - g.vx) * blend;
    g.vy += (want_y - g.vy) * blend;

    g.x += g.vx * dt;
    g.y += g.vy * dt;

    if (g.x < BALL_RADIUS)
    {
        g.x = BALL_RADIUS;
        g.vx = -g.vx * BALL_BOUNCE;
    }
    if (g.x > g.view_w - BALL_RADIUS)
    {
        g.x = g.view_w - BALL_RADIUS;
        g.vx = -g.vx * BALL_BOUNCE;
    }
    if (g.y < BALL_RADIUS)
    {
        g.y = BALL_RADIUS;
        g.vy = -g.vy * BALL_BOUNCE;
    }
    if (g.y > g.view_h - BALL_RADIUS)
    {
        g.y = g.view_h - BALL_RADIUS;
        g.vy = -g.vy * BALL_BOUNCE;
    }

    if (g.trail_count < TRAIL_LEN)
        g.trail_count++;
    for (i32 i = g.trail_count - 1; i > 0; --i)
    {
        g.trail[i * 2] = g.trail[(i - 1) * 2];
        g.trail[i * 2 + 1] = g.trail[(i - 1) * 2 + 1];
    }
    g.trail[0] = g.x;
    g.trail[1] = g.y;
}

static void emit_vertex(i32* n, f32 px, f32 py, f32 u, f32 v, f32 feather, f32 r, f32 gr, f32 b, f32 a)
{
    Ball_Vertex* out = &g.verts[(*n)++];
    out->pos[0] = 2.0f * px / g.view_w - 1.0f;
    out->pos[1] = 1.0f - 2.0f * py / g.view_h;
    out->uv[0] = u;
    out->uv[1] = v;
    out->uv[2] = feather;
    out->color[0] = r;
    out->color[1] = gr;
    out->color[2] = b;
    out->color[3] = a;
}

static void emit_sprite(i32* n, f32 cx, f32 cy, f32 radius, f32 feather_px, f32 r, f32 gr, f32 b, f32 a)
{
    f32 f = feather_px / radius;
    if (f > 1.0f)
        f = 1.0f;
    emit_vertex(n, cx - radius, cy - radius, -1, -1, f, r, gr, b, a);
    emit_vertex(n, cx + radius, cy - radius, 1, -1, f, r, gr, b, a);
    emit_vertex(n, cx + radius, cy + radius, 1, 1, f, r, gr, b, a);
    emit_vertex(n, cx - radius, cy - radius, -1, -1, f, r, gr, b, a);
    emit_vertex(n, cx + radius, cy + radius, 1, 1, f, r, gr, b, a);
    emit_vertex(n, cx - radius, cy + radius, -1, 1, f, r, gr, b, a);
}

static i32 build_scene(void)
{
    i32 n = 0;

    f32 top_r = 0.10f, top_g = 0.13f, top_b = 0.20f;
    f32 bot_r = 0.035f, bot_g = 0.043f, bot_b = 0.07f;
    emit_vertex(&n, 0, 0, 0, 0, 0.5f, top_r, top_g, top_b, 1);
    emit_vertex(&n, g.view_w, 0, 0, 0, 0.5f, top_r, top_g, top_b, 1);
    emit_vertex(&n, g.view_w, g.view_h, 0, 0, 0.5f, bot_r, bot_g, bot_b, 1);
    emit_vertex(&n, 0, 0, 0, 0, 0.5f, top_r, top_g, top_b, 1);
    emit_vertex(&n, g.view_w, g.view_h, 0, 0, 0.5f, bot_r, bot_g, bot_b, 1);
    emit_vertex(&n, 0, g.view_h, 0, 0, 0.5f, bot_r, bot_g, bot_b, 1);

    for (i32 i = g.trail_count - 1; i >= 1; --i)
    {
        f32 t = 1.0f - (f32)i / (f32)TRAIL_LEN;
        f32 radius = BALL_RADIUS * (0.25f + 0.55f * t);
        emit_sprite(&n, g.trail[i * 2], g.trail[i * 2 + 1], radius, 2.0f, 1.0f, 0.47f, 0.24f, 0.16f * t);
    }

    emit_sprite(&n, g.x, g.y, BALL_RADIUS * 2.8f, BALL_RADIUS * 2.8f, 1.0f, 0.47f, 0.24f, 0.28f);
    emit_sprite(&n, g.x, g.y, BALL_RADIUS, 2.0f, 1.0f, 0.47f, 0.24f, 1.0f);
    emit_sprite(&n, g.x - BALL_RADIUS * 0.32f, g.y - BALL_RADIUS * 0.36f, BALL_RADIUS * 0.38f, BALL_RADIUS * 0.3f, 1.0f, 0.85f, 0.7f, 0.55f);

    return n;
}

static void render(Mel_Gpu_Swapchain* sc, f64 dt, void* user)
{
    (void)user;
    if (dt > 0.1)
        dt = 0.1;
    simulate((f32)dt);

    mel_gpu_frame_begin(sc);
    Mel_Gpu_Command_List* cmd = mel_gpu_frame_commands(sc);

    if (!g.scene_ready || !g.placed)
    {
        mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0.035f, 0.043f, 0.07f, 1.0f));
        mel_gpu_cmd_end_pass(cmd);
        mel_gpu_frame_end(sc);
        return;
    }

    i32 n = build_scene();
    g.frame = (g.frame + 1) % VBO_FRAMES;
    mel_gpu_buffer_write(g.device, g.vbo[g.frame], g.verts, (usize)n * sizeof(Ball_Vertex));

    mel_gpu_cmd_begin_pass(cmd, mel_gpu_rgba(0.035f, 0.043f, 0.07f, 1.0f));
    mel_gpu_cmd_bind_pipeline(cmd, g.pipeline);
    mel_gpu_cmd_bind_vertex_buffer(cmd, 0, g.vbo[g.frame]);
    mel_gpu_cmd_draw(cmd, (u32)n, 1);
    mel_gpu_cmd_end_pass(cmd);
    mel_gpu_frame_end(sc);
}

static void scene_teardown(void)
{
    if (g.source)
    {
        mel_gpu_render_source_destroy(g.source);
        g.source = NULL;
    }
    if (g.scene_ready)
    {
        mel_gpu_pipeline_destroy(g.device, g.pipeline);
        g.scene_ready = false;
    }
    if (g.swapchain)
    {
        mel_gpu_swapchain_destroy(g.swapchain);
        g.swapchain = NULL;
    }
    if (g.surface)
    {
        mel_gpu_surface_destroy(g.surface);
        g.surface = NULL;
    }
}

static void view_resized(Mel_Gui_Handle h, i32 cw, i32 ch, void* user)
{
    (void)h;
    (void)user;

    if (cw <= 0 || ch <= 0)
    {
        scene_teardown();
        return;
    }

    g.view_w = (f32)cw;
    g.view_h = (f32)ch;
    if (!g.placed)
    {
        g.x = g.view_w * 0.5f;
        g.y = g.view_h * 0.5f;
        g.placed = true;
    }
    if (g.x > g.view_w - BALL_RADIUS)
        g.x = g.view_w - BALL_RADIUS;
    if (g.y > g.view_h - BALL_RADIUS)
        g.y = g.view_h - BALL_RADIUS;

    if (g.swapchain)
    {
        mel_gpu_swapchain_resize(g.swapchain, cw, ch);
        return;
    }
    if (!g.gpu_ready)
        return;

    void* native = mel_gpu_view_surface(g.view);
    if (!native)
        return;

    g.surface = mel_gpu_surface_create(g.device, native);
    if (!g.surface)
        return;

    g.swapchain = mel_gpu_swapchain_create(g.device, .surface = g.surface, .width = cw, .height = ch, .vsync = true);
    if (!g.swapchain)
    {
        mel_gpu_surface_destroy(g.surface);
        g.surface = NULL;
        return;
    }

    Mel_Gpu_Color_Target         target = { .format = mel_gpu_swapchain_format(g.swapchain), .blend = MEL_GPU_BLEND_ALPHA };
    const Mel_Gpu_Vertex_Element layout[] = {
        { .location = 0, .format = MEL_GPU_FORMAT_RG32_FLOAT, .offset = offsetof(Ball_Vertex, pos) },
        { .location = 1, .format = MEL_GPU_FORMAT_RGB32_FLOAT, .offset = offsetof(Ball_Vertex, uv) },
        { .location = 2, .format = MEL_GPU_FORMAT_RGBA32_FLOAT, .offset = offsetof(Ball_Vertex, color) },
    };
    Mel_Gpu_Pipeline_Create_Result pr = mel_gpu_pipeline_create(g.device,
                                                                .shader = g.shader,
                                                                .topology = MEL_GPU_TOPOLOGY_TRIANGLE_LIST,
                                                                .cull = MEL_GPU_CULL_NONE,
                                                                .color_targets = &target,
                                                                .color_target_count = 1,
                                                                .vertex_layout = layout,
                                                                .vertex_layout_count = 3,
                                                                .vertex_stride = sizeof(Ball_Vertex),
                                                                .name = "ballgame");
    if (mel_gpu_failed(pr.status))
    {
        mel_log_error("ballgame", "pipeline create failed (0x%08x)", pr.status);
        return;
    }
    g.pipeline = pr.value;
    g.scene_ready = true;

    g.source = mel_gpu_render_source_new(g.vat, g.swapchain, 60, render, NULL);
}

static void gpu_shutdown(void* user)
{
    (void)user;
    scene_teardown();
    if (g.gpu_ready)
    {
        for (i32 i = 0; i < VBO_FRAMES; ++i)
            mel_gpu_buffer_destroy(g.device, g.vbo[i]);
        mel_gpu_shader_destroy(g.device, g.shader);
        g.gpu_ready = false;
    }
    if (g.device)
    {
        mel_gpu_device_destroy(g.device);
        g.device = NULL;
    }
    if (g.instance)
    {
        mel_gpu_instance_destroy(g.instance);
        g.instance = NULL;
    }
}

static void gpu_init(Mel_Vat* vat)
{
    g.instance = mel_gpu_instance_create(.app_name = "ballgame", .debug = { .enabled = true });
    if (!g.instance)
    {
        mel_log_error("ballgame", "gpu instance create failed");
        return;
    }

    Mel_Gpu_Adapter* adapters[8];
    u32              n = mel_gpu_adapters(g.instance, adapters, 8);
    if (n == 0)
    {
        mel_log_error("ballgame", "no gpu adapters");
        return;
    }

    Mel_Gpu_Device_Create_Result dr = mel_gpu_device_create(g.instance, adapters[0], .vat = vat, .features = { .timeline_semaphores = true });
    if (mel_gpu_failed(dr.status) || !dr.value)
    {
        mel_log_error("ballgame", "gpu device create failed (0x%08x)", dr.status);
        return;
    }
    g.device = dr.value;

    Mel_Bundle_Graphics bundle = {
        .name = "ballgame",
        .spirv_vertex = BALLGAME_VERT_SPV,
        .spirv_vertex_size = sizeof BALLGAME_VERT_SPV,
        .spirv_fragment = BALLGAME_FRAG_SPV,
        .spirv_fragment_size = sizeof BALLGAME_FRAG_SPV,
#if BALLGAME_HAS_MSL
        .msl_vertex = BALLGAME_VERT_MSL,
        .msl_vertex_size = sizeof BALLGAME_VERT_MSL,
        .msl_fragment = BALLGAME_FRAG_MSL,
        .msl_fragment_size = sizeof BALLGAME_FRAG_MSL,
#endif
#if BALLGAME_HAS_WGSL
        .wgsl_vertex = BALLGAME_VERT_WGSL,
        .wgsl_vertex_size = sizeof BALLGAME_VERT_WGSL,
        .wgsl_fragment = BALLGAME_FRAG_WGSL,
        .wgsl_fragment_size = sizeof BALLGAME_FRAG_WGSL,
#endif
        .vertex_entry = BALLGAME_VERT_ENTRY,
        .fragment_entry = BALLGAME_FRAG_ENTRY,
    };
    Mel_Gpu_Shader_Create_Result sr = mel_bundle_select_graphics(g.device, &bundle);
    if (mel_gpu_failed(sr.status))
    {
        mel_log_error("ballgame", "shader create failed (0x%08x)", sr.status);
        return;
    }
    g.shader = sr.value;

    for (i32 i = 0; i < VBO_FRAMES; ++i)
    {
        Mel_Gpu_Buffer_Create_Result br = mel_gpu_buffer_create(g.device, .size = VERT_CAP * sizeof(Ball_Vertex), .usage = MEL_GPU_BUFFER_VERTEX, .memory = MEL_GPU_MEMORY_UPLOAD, .name = "ballgame-vbo");
        if (mel_gpu_failed(br.status))
        {
            mel_log_error("ballgame", "vertex buffer create failed (0x%08x)", br.status);
            return;
        }
        g.vbo[i] = br.value;
    }

    g.verts = mel_alloc_array(mel_alloc_heap(), Ball_Vertex, VERT_CAP);
    g.trail = mel_alloc_array(mel_alloc_heap(), f32, TRAIL_LEN * 2);
    g.gpu_ready = true;

    mel_app_on_exit(gpu_shutdown, NULL);
}

static void build_game(Mel_Gui_Handle frame, void* user)
{
    (void)user;
    mel_gui_set_text(frame, S8("Ball Game"));
    mel_gui_set_layout(frame, mel_column_layout(.cross_align = MEL_ALIGN_STRETCH));
    g.view = mel_gpu_view_create(frame,
                                 .on_.on_resize = view_resized,
                                 .keyboard = { .on_key_down = on_key_down, .on_key_up = on_key_up },
                                 .pointer = { .on_pointer_down = on_pointer_down, .on_pointer_move = on_pointer_move, .on_pointer_up = on_pointer_up },
                                 .layoutable = { .preferred_w = 800, .preferred_h = 600, .weight = 1 });
    mel_gui_set_focus(g.view);
}

void mel_app_setup(Mel_Vat* root)
{
    g.vat = root;
    g.view = MEL_GUI_HANDLE_NONE;
    mel_gui_init(root);
    gpu_init(root);
    mel_app_register_screen(S8("game"), build_game, NULL);
    mel_app_present(S8("game"), NULL);
}
