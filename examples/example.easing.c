#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define CIMGUI_USE_SDL3
#define CIMGUI_USE_VULKAN
#include <volk.h>

#include "core.app.h"
#include "core.engine.h"
#include "window.h"
#include "swapchain.h"
#include "gpu.swapchain.h"
#include "string.str8.h"
#include "gpu.pipeline.h"
#include "gpu.buffer.h"
#include "gpu.cmd.h"
#include "sprite.pass.h"
#include "render.graph.h"
#include "render.list.h"
#include "render.target.h"
#include "render.camera.h"
#include "texture.pool.h"
#include "font.atlas.h"
#include "vfs.h"
#include "vfs.backend.os.h"
#include "allocator.heap.h"
#include "sim.ctx.h"
#include "math.mat4.h"
#include "math.vec4.h"
#include "math.vec2.h"
#include "math.geo.rect.h"
#include "math.easing.h"

#define EASING_COUNT       31
#define GRID_COLS          7
#define GRID_ROWS          5
#define PANEL_W            140.0f
#define PANEL_H            100.0f
#define PANEL_PAD          12.0f
#define PANEL_INNER_PAD    8.0f
#define GRID_MARGIN_X      30.0f
#define GRID_MARGIN_Y      20.0f
#define CURVE_SAMPLES      50
#define FOCUS_SAMPLES      100
#define FOCUS_W            500.0f
#define FOCUS_H            200.0f
#define FOCUS_BAR_H        20.0f
#define DOT_SIZE           3.0f
#define ANIM_DOT_SIZE      6.0f
#define ANIM_PERIOD        2.0f

typedef struct {
    const char* name;
    f32 (*func)(f32);
} EasingEntry;

typedef struct {
    EasingEntry entries[EASING_COUNT];
    f32 time;
    bool paused;
    i32 hovered;
    i32 selected;
} EasingDemo;

static Mel_Window_Handle s_window_handle;
static Mel_Swapchain_Handle s_swapchain_handle;
static Mel_Sprite_Pass* s_sp;
static Mel_Font_Atlas_Pool s_font_pool;
static Mel_Io s_demo_io;
static Mel_Vfs s_demo_vfs;
static Mel_Vfs_Backend* s_fonts_backend;
static Mel_Font_Handle s_font_handle;
static EasingDemo s_demo;
static Mel_Render_Target s_swapchain_target;
static Mel_Render_Graph s_graph;
static Mel_Camera s_camera;
static Mel_Render_List s_sprite_list;
static Mel_Render_List s_font_list;
static Mel_Sim_Ctx s_sim;
static u8 s_event_buf[4096];

static void demo_init(EasingDemo* d)
{
    d->time = 0.0f;
    d->paused = false;
    d->hovered = -1;
    d->selected = -1;

    i32 i = 0;
    d->entries[i++] = (EasingEntry){ "linear",         mel_ease_linear };
    d->entries[i++] = (EasingEntry){ "in_quad",        mel_ease_in_quad };
    d->entries[i++] = (EasingEntry){ "out_quad",       mel_ease_out_quad };
    d->entries[i++] = (EasingEntry){ "in_out_quad",    mel_ease_in_out_quad };
    d->entries[i++] = (EasingEntry){ "in_cubic",       mel_ease_in_cubic };
    d->entries[i++] = (EasingEntry){ "out_cubic",      mel_ease_out_cubic };
    d->entries[i++] = (EasingEntry){ "in_out_cubic",   mel_ease_in_out_cubic };
    d->entries[i++] = (EasingEntry){ "in_quart",       mel_ease_in_quart };
    d->entries[i++] = (EasingEntry){ "out_quart",      mel_ease_out_quart };
    d->entries[i++] = (EasingEntry){ "in_out_quart",   mel_ease_in_out_quart };
    d->entries[i++] = (EasingEntry){ "in_quint",       mel_ease_in_quint };
    d->entries[i++] = (EasingEntry){ "out_quint",      mel_ease_out_quint };
    d->entries[i++] = (EasingEntry){ "in_out_quint",   mel_ease_in_out_quint };
    d->entries[i++] = (EasingEntry){ "in_sine",        mel_ease_in_sine };
    d->entries[i++] = (EasingEntry){ "out_sine",       mel_ease_out_sine };
    d->entries[i++] = (EasingEntry){ "in_out_sine",    mel_ease_in_out_sine };
    d->entries[i++] = (EasingEntry){ "in_circ",        mel_ease_in_circ };
    d->entries[i++] = (EasingEntry){ "out_circ",       mel_ease_out_circ };
    d->entries[i++] = (EasingEntry){ "in_out_circ",    mel_ease_in_out_circ };
    d->entries[i++] = (EasingEntry){ "in_expo",        mel_ease_in_expo };
    d->entries[i++] = (EasingEntry){ "out_expo",       mel_ease_out_expo };
    d->entries[i++] = (EasingEntry){ "in_out_expo",    mel_ease_in_out_expo };
    d->entries[i++] = (EasingEntry){ "in_elastic",     mel_ease_in_elastic };
    d->entries[i++] = (EasingEntry){ "out_elastic",    mel_ease_out_elastic };
    d->entries[i++] = (EasingEntry){ "in_out_elastic", mel_ease_in_out_elastic };
    d->entries[i++] = (EasingEntry){ "in_back",        mel_ease_in_back };
    d->entries[i++] = (EasingEntry){ "out_back",       mel_ease_out_back };
    d->entries[i++] = (EasingEntry){ "in_out_back",    mel_ease_in_out_back };
    d->entries[i++] = (EasingEntry){ "in_bounce",      mel_ease_in_bounce };
    d->entries[i++] = (EasingEntry){ "out_bounce",     mel_ease_out_bounce };
    d->entries[i++] = (EasingEntry){ "in_out_bounce",  mel_ease_in_out_bounce };
}

static void panel_pos(i32 index, f32* out_x, f32* out_y)
{
    i32 col = index % GRID_COLS;
    i32 row = index / GRID_COLS;
    *out_x = GRID_MARGIN_X + (f32)col * (PANEL_W + PANEL_PAD);
    *out_y = GRID_MARGIN_Y + (f32)row * (PANEL_H + PANEL_PAD);
}

static i32 hit_test_panel(f32 mx, f32 my)
{
    for (i32 i = 0; i < EASING_COUNT; i++)
    {
        f32 px, py;
        panel_pos(i, &px, &py);
        if (mx >= px && mx < px + PANEL_W && my >= py && my < py + PANEL_H)
            return i;
    }
    return -1;
}

static void push_sprite(Mel_Render_List* list, f32 x, f32 y, f32 w, f32 h, Mel_Vec4 color)
{
    mel_draw_sprite(list, .pos = mel_vec2(x, y), .size = mel_vec2(w, h), .color = color);
}

static void draw_curve_dots(Mel_Render_List* list, f32 px, f32 py, f32 w, f32 h,
                            f32 pad, i32 samples, f32 (*func)(f32), Mel_Vec4 color)
{
    f32 plot_x = px + pad;
    f32 plot_y = py + pad + 14.0f;
    f32 plot_w = w - pad * 2.0f;
    f32 plot_h = h - pad * 2.0f - 14.0f;

    for (i32 i = 0; i <= samples; i++)
    {
        f32 t = (f32)i / (f32)samples;
        f32 v = func(t);
        f32 x = plot_x + t * plot_w;
        f32 y = plot_y + plot_h - v * plot_h;
        push_sprite(list, x - DOT_SIZE * 0.5f, y - DOT_SIZE * 0.5f,
                    DOT_SIZE, DOT_SIZE, color);
    }
}

static void draw_anim_dot(Mel_Render_List* list, f32 px, f32 py, f32 w, f32 h,
                           f32 pad, f32 t, f32 (*func)(f32), Mel_Vec4 color)
{
    f32 plot_x = px + pad;
    f32 plot_y = py + pad + 14.0f;
    f32 plot_w = w - pad * 2.0f;
    f32 plot_h = h - pad * 2.0f - 14.0f;

    f32 v = func(t);
    f32 x = plot_x + t * plot_w;
    f32 y = plot_y + plot_h - v * plot_h;
    push_sprite(list, x - ANIM_DOT_SIZE * 0.5f, y - ANIM_DOT_SIZE * 0.5f,
                ANIM_DOT_SIZE, ANIM_DOT_SIZE, color);
}

static void draw_shapes(EasingDemo* d, Mel_Render_List* list, f32 win_w, f32 win_h)
{
    MEL_UNUSED(win_w);
    MEL_UNUSED(win_h);

    f32 t = fmodf(d->time, ANIM_PERIOD) / ANIM_PERIOD;

    Mel_Vec4 bg_normal   = mel_vec4(0.14f, 0.14f, 0.18f, 1.0f);
    Mel_Vec4 bg_hovered  = mel_vec4(0.20f, 0.20f, 0.26f, 1.0f);
    Mel_Vec4 bg_selected = mel_vec4(0.22f, 0.18f, 0.28f, 1.0f);
    Mel_Vec4 curve_color = mel_vec4(0.4f, 0.7f, 1.0f, 1.0f);
    Mel_Vec4 dot_color   = mel_vec4(1.0f, 0.4f, 0.3f, 1.0f);

    for (i32 i = 0; i < EASING_COUNT; i++)
    {
        f32 px, py;
        panel_pos(i, &px, &py);

        Mel_Vec4 bg = bg_normal;
        if (i == d->selected) bg = bg_selected;
        else if (i == d->hovered) bg = bg_hovered;

        push_sprite(list, px, py, PANEL_W, PANEL_H, bg);

        draw_curve_dots(list, px, py, PANEL_W, PANEL_H,
                        PANEL_INNER_PAD, CURVE_SAMPLES, d->entries[i].func, curve_color);

        draw_anim_dot(list, px, py, PANEL_W, PANEL_H,
                      PANEL_INNER_PAD, t, d->entries[i].func, dot_color);
    }

    i32 focus_idx = d->selected >= 0 ? d->selected : d->hovered;
    if (focus_idx < 0) focus_idx = 0;

    f32 grid_bottom = GRID_MARGIN_Y + (f32)GRID_ROWS * (PANEL_H + PANEL_PAD);
    f32 focus_x = GRID_MARGIN_X;
    f32 focus_y = grid_bottom + 10.0f;

    Mel_Vec4 focus_bg = mel_vec4(0.12f, 0.12f, 0.16f, 1.0f);
    push_sprite(list, focus_x, focus_y, FOCUS_W, FOCUS_H, focus_bg);

    draw_curve_dots(list, focus_x, focus_y, FOCUS_W, FOCUS_H,
                    PANEL_INNER_PAD, FOCUS_SAMPLES, d->entries[focus_idx].func, curve_color);

    draw_anim_dot(list, focus_x, focus_y, FOCUS_W, FOCUS_H,
                  PANEL_INNER_PAD, t, d->entries[focus_idx].func, dot_color);

    f32 bar_x = focus_x + FOCUS_W + 20.0f;
    f32 bar_y = focus_y + FOCUS_H * 0.5f - FOCUS_BAR_H * 0.5f;
    f32 bar_w = 300.0f;

    Mel_Vec4 bar_bg = mel_vec4(0.08f, 0.08f, 0.1f, 1.0f);
    push_sprite(list, bar_x, bar_y, bar_w, FOCUS_BAR_H, bar_bg);

    f32 eased = d->entries[focus_idx].func(t);
    f32 fill_w = mel_clampf(eased, 0.0f, 1.5f) * bar_w;
    Mel_Vec4 bar_fill = mel_vec4(0.3f, 0.8f, 0.5f, 1.0f);
    push_sprite(list, bar_x, bar_y, fill_w, FOCUS_BAR_H, bar_fill);
}

static void draw_text(EasingDemo* d, Mel_Render_List* list,
                      Mel_Font_Atlas_Pool* pool, Mel_Font_Handle font)
{
    Mel_Vec4 label_color = mel_vec4(0.7f, 0.7f, 0.7f, 1.0f);
    Mel_Vec4 white = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    Mel_Vec4 dim = mel_vec4(0.5f, 0.5f, 0.5f, 1.0f);

    for (i32 i = 0; i < EASING_COUNT; i++)
    {
        f32 px, py;
        panel_pos(i, &px, &py);
        mel_font_atlas_draw_text(pool, font, list, str8_from_cstr(d->entries[i].name),
                                 px + PANEL_INNER_PAD, py + 4.0f, label_color);
    }

    i32 focus_idx = d->selected >= 0 ? d->selected : d->hovered;
    if (focus_idx < 0) focus_idx = 0;

    f32 grid_bottom = GRID_MARGIN_Y + (f32)GRID_ROWS * (PANEL_H + PANEL_PAD);
    f32 focus_x = GRID_MARGIN_X;
    f32 focus_y = grid_bottom + 10.0f;

    char full_name[64];
    snprintf(full_name, sizeof(full_name), "mel_ease_%s", d->entries[focus_idx].name);
    mel_font_atlas_draw_text(pool, font, list, str8_from_cstr(full_name),
                             focus_x + PANEL_INNER_PAD, focus_y + 2.0f, white);

    f32 bar_x = focus_x + FOCUS_W + 20.0f;
    f32 bar_y = focus_y + FOCUS_H * 0.5f - FOCUS_BAR_H * 0.5f;
    mel_font_atlas_draw_text(pool, font, list, S8("eased output"),
                             bar_x, bar_y - 18.0f, dim);

    mel_font_atlas_draw_text(pool, font, list,
                             d->paused ? S8("[PAUSED] Space=resume  R=reset  ESC=quit")
                                       : S8("Space=pause  R=reset  Click=select  ESC=quit"),
                             GRID_MARGIN_X, focus_y + FOCUS_H + 10.0f, dim);
}

static void on_init(void)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;
    s_sp = mel_sprite_pass();

    mel_font_atlas_pool_init(&s_font_pool, mel_allocator(), dev, &s_demo_vfs, .texture_pool = mel_texture_pool());
    s_font_handle = mel_font_atlas_pool_load(&s_font_pool,
        .path = S8("/System/Library/Fonts/Monaco.ttf"), .size = 18.0f);


    demo_init(&s_demo);

    mel_render_list_init(&s_sprite_list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    mel_render_list_init(&s_font_list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    mel_render_target_init_swapchain(&s_swapchain_target, sc, dev, S8("backbuffer"));

    s_camera = (Mel_Camera){
        .view = MEL_MAT4_IDENTITY,
        .projection = mel_mat4_ortho(0, (f32)sc->extent.width,
                                      0, (f32)sc->extent.height, -1, 1),
    };

    mel_render_graph_init(&s_graph, .dev = dev, .alloc = mel_alloc_heap());
    mel_render_graph_add_pass(&s_graph, S8("render"),
        .fn = mel_sprite_pass_execute,
        .user = s_sp,
        .camera = &s_camera,
        .read_lists = MEL_LISTS(&s_sprite_list, &s_font_list),
        .write_targets = MEL_WRITE_TARGETS(
            { .target = &s_swapchain_target, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .clear.color = { .r = 0.08f, .g = 0.08f, .b = 0.1f, .a = 1.0f } }));
    mel_render_graph_compile(&s_graph);
    mel_set_render_graph(&s_graph);
}

static void app_update(Mel_Sim_Ctx* sim, f32 dt, void* user);

static void app_init(Mel_App* app)
{
    mel_init(.app_name = S8("Melody Easing"), .enable_validation = true);
    s_window_handle = mel_window_create(S8("Melody Easing Visualizer"), .width = 1200, .height = 800);
    s_swapchain_handle = mel_gpu_swapchain_create_for_window(mel_gpu_dev(), s_window_handle);

    Mel_Io_Desc io_desc = { .allocator = mel_alloc_heap(), .worker_count = 0 };
    mel_io_init(&s_demo_io, &io_desc);
    Mel_Vfs_Desc vfs_desc = { .allocator = mel_alloc_heap(), .io = &s_demo_io };
    mel_vfs_init(&s_demo_vfs, &vfs_desc);
    s_fonts_backend = mel_vfs_backend_os_create(mel_alloc_heap(), S8("/"));
    mel_vfs_mount(&s_demo_vfs, S8("/"), s_fonts_backend, 0, false);

    on_init();

    mel_sim_init(&s_sim, .event_buffer = s_event_buf, .event_buffer_size = sizeof(s_event_buf));
    mel_sim_add_variable(&s_sim, app_update);
    mel_register_sim(&s_sim);
}

static void app_shutdown(Mel_App* app)
{
    mel_unregister_sim(&s_sim);
    mel_sim_shutdown(&s_sim);

    mel_render_list_shutdown(&s_sprite_list);
    mel_render_list_shutdown(&s_font_list);
    mel_render_graph_shutdown(&s_graph);
    mel_render_target_shutdown(&s_swapchain_target);

    mel_font_atlas_pool_shutdown(&s_font_pool);
    mel_vfs_unmount(&s_demo_vfs, S8("/"));
    mel_vfs_shutdown(&s_demo_vfs);
    mel_io_shutdown(&s_demo_io);
    mel_vfs_backend_os_destroy(s_fonts_backend);
}

static void app_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(user);

    float mx, my;
    SDL_GetMouseState(&mx, &my);
    s_demo.hovered = hit_test_panel(mx, my);

    if (!s_demo.paused)
        s_demo.time += dt;

    mel_render_list_clear(&s_sprite_list);
    mel_render_list_clear(&s_font_list);

    draw_shapes(&s_demo, &s_sprite_list, 0.0f, 0.0f);

    draw_text(&s_demo, &s_font_list, &s_font_pool, s_font_handle);
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
        switch (event->key.scancode)
        {
            case SDL_SCANCODE_SPACE:
                s_demo.paused = !s_demo.paused;
                break;
            case SDL_SCANCODE_R:
                s_demo.time = 0.0f;
                break;
            default:
                break;
        }
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT)
    {
        i32 hit = hit_test_panel(event->button.x, event->button.y);
        if (hit >= 0)
        {
            if (s_demo.selected == hit)
                s_demo.selected = -1;
            else
                s_demo.selected = hit;
        }
    }
}

MEL_APP(
    .on_init = app_init,
    .on_shutdown = app_shutdown,
    .on_event = app_event
)
