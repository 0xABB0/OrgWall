#include <SDL3/SDL.h>

#define CIMGUI_USE_SDL3
#define CIMGUI_USE_VULKAN
#include <volk.h>

#include "core.app.h"
#include "core.engine.h"
#include "window.h"
#include "swapchain.h"
#include "gpu.swapchain.h"
#include "gpu.device.h"
#include "gpu.texture.h"
#include "string.str8.h"
#include "sprite.pass.h"
#include "render.graph.h"
#include "render.list.h"
#include "render.target.h"
#include "render.camera.h"
#include "texture.pool.h"
#include "font.atlas.h"
#include "vfs.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "async.job.h"
#include "sim.ctx.h"
#include "math.mat4.h"
#include "math.vec4.h"
#include "math.vec2.h"

#include <math.h>
#include <string.h>

#define WIDTH  960
#define HEIGHT 720

#define MAX_ITER_INIT 256
#define MAX_ITER_CAP  4096
#define ZOOM_SPEED    1.15

typedef struct {
    u8* pixels;
    i32 width;
    i32 height;
    f64 center_x;
    f64 center_y;
    f64 zoom;
    i32 max_iter;
} Fractal_Compute;

typedef struct {
    f64 center_x, center_y;
    f64 zoom;
    i32 max_iter;
    bool dirty;
    bool dragging;
    f32 drag_start_mx, drag_start_my;
    f64 drag_start_cx, drag_start_cy;
} Fractal_State;

static Mel_Window_Handle      s_window_handle;
static Mel_Swapchain_Handle   s_swapchain_handle;
static Mel_Font_Handle        s_font_handle;
static Mel_Render_Graph       s_graph;
static Mel_Render_Target      s_swapchain_target;
static Mel_Camera             s_camera;
static Mel_Render_List        s_sprite_list;
static Mel_Render_List        s_font_list;
static Mel_Sim_Ctx            s_sim;
static u8                     s_event_buf[4096];

static Mel_Job_Context*       s_job_ctx;
static Fractal_State          s_fractal;
static Fractal_Compute        s_compute;
static u8*                    s_pixel_buf;
static Mel_Gpu_Texture        s_fractal_tex;
static Mel_Texture_Handle     s_fractal_handle;
static bool                   s_tex_alive;

static void fractal_color(i32 iter, i32 max_iter, u8* r, u8* g, u8* b)
{
    if (iter >= max_iter)
    {
        *r = *g = *b = 0;
        return;
    }

    f64 t = (f64)iter / (f64)max_iter;
    f64 smooth_t = sqrt(t);

    f64 phase = smooth_t * 6.2831853 * 3.0;
    *r = (u8)(127.5 + 127.5 * cos(phase));
    *g = (u8)(127.5 + 127.5 * cos(phase + 2.094395));
    *b = (u8)(127.5 + 127.5 * cos(phase + 4.18879));
}

static void fractal_compute_rows(i32 range_start, i32 range_end, i32 thread_index, void* user)
{
    MEL_UNUSED(thread_index);
    Fractal_Compute* fc = (Fractal_Compute*)user;

    f64 aspect = (f64)fc->width / (f64)fc->height;
    f64 half_h = 2.0 / fc->zoom;
    f64 half_w = half_h * aspect;

    for (i32 y = range_start; y < range_end; y++)
    {
        f64 ci = fc->center_y + half_h * ((f64)y / (f64)fc->height - 0.5) * 2.0;

        for (i32 x = 0; x < fc->width; x++)
        {
            f64 cr = fc->center_x + half_w * ((f64)x / (f64)fc->width - 0.5) * 2.0;

            f64 zr = 0.0, zi = 0.0;
            i32 iter = 0;

            while (zr * zr + zi * zi <= 4.0 && iter < fc->max_iter)
            {
                f64 tmp = zr * zr - zi * zi + cr;
                zi = 2.0 * zr * zi + ci;
                zr = tmp;
                iter++;
            }

            i32 idx = (y * fc->width + x) * 4;
            fractal_color(iter, fc->max_iter, &fc->pixels[idx], &fc->pixels[idx + 1], &fc->pixels[idx + 2]);
            fc->pixels[idx + 3] = 255;
        }
    }
}

static void fractal_recompute(void)
{
    s_compute = (Fractal_Compute){
        .pixels   = s_pixel_buf,
        .width    = WIDTH,
        .height   = HEIGHT,
        .center_x = s_fractal.center_x,
        .center_y = s_fractal.center_y,
        .zoom     = s_fractal.zoom,
        .max_iter = s_fractal.max_iter,
    };

    Mel_Job job = mel_job_dispatch(s_job_ctx, HEIGHT, fractal_compute_rows, &s_compute);
    mel_job_wait_and_del(s_job_ctx, job);

    Mel_Gpu_Device* dev = mel_gpu_dev();
    Mel_Texture_Pool* pool = mel_texture_pool();

    if (s_tex_alive)
    {
        mel_gpu_device_wait_idle(dev);
        mel_texture_pool_unload(pool, s_fractal_handle);
        s_tex_alive = false;
    }

    mel_gpu_texture_init(&s_fractal_tex, dev,
        .pixels = s_pixel_buf,
        .width  = WIDTH,
        .height = HEIGHT,
        .nearest_filter = true);

    s_fractal_handle = mel_texture_pool_register(pool, &s_fractal_tex);
    s_tex_alive = true;
    s_fractal.dirty = false;
}

static void on_init(void)
{
    Mel_Gpu_Device* dev = mel_gpu_dev();
    Mel_Swapchain* sc = &mel_swapchain_registry_get(s_swapchain_handle)->swapchain;

    s_font_handle = mel_font_atlas_pool_load(mel_font_pool(),
        .path = S8("/System/Library/Fonts/Monaco.ttf"), .size = 16.0f);

    s_job_ctx = mel_job_create_context(mel_alloc_heap());

    s_pixel_buf = mel_alloc(mel_alloc_heap(), (usize)(WIDTH * HEIGHT * 4));

    s_fractal = (Fractal_State){
        .center_x = -0.5,
        .center_y = 0.0,
        .zoom     = 1.0,
        .max_iter = MAX_ITER_INIT,
        .dirty    = true,
    };

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
        .user = mel_sprite_pass(),
        .camera = &s_camera,
        .read_lists = MEL_LISTS(&s_sprite_list, &s_font_list),
        .write_targets = MEL_WRITE_TARGETS(
            { .target = &s_swapchain_target, .load_op = VK_ATTACHMENT_LOAD_OP_CLEAR,
              .clear.color = { .r = 0.02f, .g = 0.02f, .b = 0.04f, .a = 1.0f } }));
    mel_render_graph_compile(&s_graph);
    mel_set_render_graph(&s_graph);

    fractal_recompute();

    i32 threads = mel_job_num_worker_threads(s_job_ctx);
    SDL_Log("Mandelbrot ready! %d worker threads + main thread", threads);
    SDL_Log("Scroll=zoom, Drag=pan, +/-=iterations, R=reset, ESC=quit");
}

static void app_update(Mel_Sim_Ctx* sim, f32 dt, void* user);

Mel_App_Config app_config(void)
{
    return (Mel_App_Config){
        .app_name = S8("Melody Jobs - Mandelbrot"),
        .enable_validation = true,
    };
}

void app_init(void)
{
    s_window_handle = mel_window_create(S8("Melody Jobs - Mandelbrot"), .width = WIDTH, .height = HEIGHT);
    s_swapchain_handle = mel_gpu_swapchain_create_for_window(mel_gpu_dev(), s_window_handle);
    mel_vfs_mount_native(mel_vfs(), S8("/"), S8("/"), 0, false);

    on_init();

    mel_sim_init(&s_sim, .event_buffer = s_event_buf, .event_buffer_size = sizeof(s_event_buf));
    mel_sim_add_variable(&s_sim, app_update);
    mel_register_sim(&s_sim);
}

void app_shutdown(void)
{
    mel_unregister_sim(&s_sim);
    mel_sim_shutdown(&s_sim);

    if (s_tex_alive)
    {
        mel_gpu_device_wait_idle(mel_gpu_dev());
        mel_texture_pool_unload(mel_texture_pool(), s_fractal_handle);
    }

    mel_job_destroy_context(s_job_ctx, mel_alloc_heap());
    mel_dealloc(mel_alloc_heap(), s_pixel_buf);

    mel_render_list_shutdown(&s_sprite_list);
    mel_render_list_shutdown(&s_font_list);
    mel_render_graph_shutdown(&s_graph);
    mel_render_target_shutdown(&s_swapchain_target);

    mel_vfs_unmount(mel_vfs(), S8("/"));
}

static void app_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(dt);
    MEL_UNUSED(user);

    if (s_fractal.dirty)
        fractal_recompute();

    mel_render_list_clear(&s_sprite_list);
    mel_render_list_clear(&s_font_list);

    mel_draw_sprite(&s_sprite_list,
        .pos  = mel_vec2(0.0f, 0.0f),
        .size = mel_vec2((f32)WIDTH, (f32)HEIGHT),
        .color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
        .tex  = s_fractal_handle);

    Mel_Font_Atlas_Pool* fp = mel_font_pool();
    Mel_Vec4 white = mel_vec4(1.0f, 1.0f, 1.0f, 0.9f);
    Mel_Vec4 dim   = mel_vec4(0.6f, 0.6f, 0.6f, 0.8f);

    char buf[256];
    i32 threads = mel_job_num_worker_threads(s_job_ctx) + 1;
    snprintf(buf, sizeof(buf), "zoom: %.2e  center: (%.10f, %.10f)  iter: %d  threads: %d",
             s_fractal.zoom, s_fractal.center_x, s_fractal.center_y,
             s_fractal.max_iter, threads);
    mel_font_atlas_draw_text(fp, s_font_handle, &s_font_list, str8_from_cstr(buf), 8.0f, 4.0f, white);

    mel_font_atlas_draw_text(fp, s_font_handle, &s_font_list,
        S8("scroll=zoom  drag=pan  +/-=iter  R=reset  ESC=quit"),
        8.0f, 22.0f, dim);
}

static void zoom_at(f32 mx, f32 my, f64 factor)
{
    f64 aspect = (f64)WIDTH / (f64)HEIGHT;
    f64 half_h = 2.0 / s_fractal.zoom;
    f64 half_w = half_h * aspect;

    f64 world_x = s_fractal.center_x + half_w * ((f64)mx / (f64)WIDTH - 0.5) * 2.0;
    f64 world_y = s_fractal.center_y + half_h * ((f64)my / (f64)HEIGHT - 0.5) * 2.0;

    s_fractal.zoom *= factor;

    f64 new_half_h = 2.0 / s_fractal.zoom;
    f64 new_half_w = new_half_h * aspect;

    s_fractal.center_x = world_x - new_half_w * ((f64)mx / (f64)WIDTH - 0.5) * 2.0;
    s_fractal.center_y = world_y - new_half_h * ((f64)my / (f64)HEIGHT - 0.5) * 2.0;

    s_fractal.dirty = true;
}

void app_event(SDL_Event* event)
{
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE)
    {
        mel_quit();
        return;
    }

    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat)
    {
        switch (event->key.scancode)
        {
            case SDL_SCANCODE_R:
                s_fractal.center_x = -0.5;
                s_fractal.center_y = 0.0;
                s_fractal.zoom     = 1.0;
                s_fractal.max_iter = MAX_ITER_INIT;
                s_fractal.dirty    = true;
                break;
            case SDL_SCANCODE_EQUALS:
            case SDL_SCANCODE_KP_PLUS:
                s_fractal.max_iter *= 2;
                if (s_fractal.max_iter > MAX_ITER_CAP)
                    s_fractal.max_iter = MAX_ITER_CAP;
                s_fractal.dirty = true;
                break;
            case SDL_SCANCODE_MINUS:
            case SDL_SCANCODE_KP_MINUS:
                s_fractal.max_iter /= 2;
                if (s_fractal.max_iter < 32)
                    s_fractal.max_iter = 32;
                s_fractal.dirty = true;
                break;
            default: break;
        }
    }

    if (event->type == SDL_EVENT_MOUSE_WHEEL)
    {
        f32 mx, my;
        SDL_GetMouseState(&mx, &my);
        f64 factor = event->wheel.y > 0 ? ZOOM_SPEED : (1.0 / ZOOM_SPEED);
        zoom_at(mx, my, factor);
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT)
    {
        s_fractal.dragging = true;
        s_fractal.drag_start_mx = event->button.x;
        s_fractal.drag_start_my = event->button.y;
        s_fractal.drag_start_cx = s_fractal.center_x;
        s_fractal.drag_start_cy = s_fractal.center_y;
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT)
    {
        s_fractal.dragging = false;
    }

    if (event->type == SDL_EVENT_MOUSE_MOTION && s_fractal.dragging)
    {
        f64 aspect = (f64)WIDTH / (f64)HEIGHT;
        f64 half_h = 2.0 / s_fractal.zoom;
        f64 half_w = half_h * aspect;

        f64 dx = (f64)(event->motion.x - s_fractal.drag_start_mx) / (f64)WIDTH * half_w * 2.0;
        f64 dy = (f64)(event->motion.y - s_fractal.drag_start_my) / (f64)HEIGHT * half_h * 2.0;

        s_fractal.center_x = s_fractal.drag_start_cx - dx;
        s_fractal.center_y = s_fractal.drag_start_cy - dy;
        s_fractal.dirty = true;
    }
}
