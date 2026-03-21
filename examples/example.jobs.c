#include <SDL3/SDL.h>

#define CIMGUI_USE_SDL3

#include "core.app.h"
#include "core.engine.h"
#include "log.h"
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
#include "font.desc.h"
#include "allocator.h"
#include "allocator.heap.h"
#include "vfs.h"
#include "vfs.backend.os.h"
#include "async.job.h"
#include "async.signal.h"
#include <stdatomic.h>
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
    f64 julia_cr;
    f64 julia_ci;
    bool is_julia;
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

typedef struct {
    Fractal_Compute* compute;
    _Atomic(i32) next_row;
} Fractal_Job_Data;

typedef struct {
    Mel_Window_Handle    window;
    Mel_Swapchain_Handle swapchain;
    Mel_Sprite_Pass      sprite_pass;
    Mel_Render_Target    target;
    Mel_Camera           camera;
    Mel_Render_List      sprite_list;
    Mel_Render_List      font_list;
    u8*                  pixel_buf;
    Mel_Gpu_Texture      tex;
    Mel_Texture_Handle   tex_handle;
    bool                 tex_alive;
    Fractal_State        state;
    Fractal_Compute      compute;
} Fractal_Window;

static Fractal_Window     s_mandelbrot;
static Fractal_Window     s_julia;
static Mel_Font_Atlas_Handle    s_font_handle;
static Mel_Render_Graph   s_graph;
static Mel_Sim_Ctx        s_sim;
static u8                 s_event_buf[4096];
static f64                s_julia_cr;
static f64                s_julia_ci;
static bool               s_julia_dirty;
static u32                s_mandelbrot_window_id;
static u32                s_julia_window_id;

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

static void mandelbrot_row(Fractal_Compute* fc, i32 y)
{
    f64 aspect = (f64)fc->width / (f64)fc->height;
    f64 half_h = 2.0 / fc->zoom;
    f64 half_w = half_h * aspect;
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

static void julia_row(Fractal_Compute* fc, i32 y)
{
    f64 aspect = (f64)fc->width / (f64)fc->height;
    f64 half_h = 2.0 / fc->zoom;
    f64 half_w = half_h * aspect;
    f64 zi0 = fc->center_y + half_h * ((f64)y / (f64)fc->height - 0.5) * 2.0;

    for (i32 x = 0; x < fc->width; x++)
    {
        f64 zr = fc->center_x + half_w * ((f64)x / (f64)fc->width - 0.5) * 2.0;
        f64 zi = zi0;
        i32 iter = 0;

        while (zr * zr + zi * zi <= 4.0 && iter < fc->max_iter)
        {
            f64 tmp = zr * zr - zi * zi + fc->julia_cr;
            zi = 2.0 * zr * zi + fc->julia_ci;
            zr = tmp;
            iter++;
        }

        i32 idx = (y * fc->width + x) * 4;
        fractal_color(iter, fc->max_iter, &fc->pixels[idx], &fc->pixels[idx + 1], &fc->pixels[idx + 2]);
        fc->pixels[idx + 3] = 255;
    }
}

typedef void (*Fractal_Row_Fn)(Fractal_Compute*, i32);

typedef struct {
    Fractal_Job_Data* job;
    Fractal_Row_Fn row_fn;
} Fractal_Worker_Data;

static void fractal_worker(void* arg)
{
    Fractal_Worker_Data* wd = (Fractal_Worker_Data*)arg;
    Fractal_Compute* fc = wd->job->compute;

    for (;;)
    {
        i32 row = atomic_fetch_add_explicit(&wd->job->next_row, 1, memory_order_relaxed);
        if (row >= fc->height)
            break;
        wd->row_fn(fc, row);
    }
}

static void fractal_recompute(Fractal_Window* fw, Fractal_Row_Fn row_fn)
{
    fw->compute = (Fractal_Compute){
        .pixels   = fw->pixel_buf,
        .width    = WIDTH,
        .height   = HEIGHT,
        .center_x = fw->state.center_x,
        .center_y = fw->state.center_y,
        .zoom     = fw->state.zoom,
        .max_iter = fw->state.max_iter,
        .julia_cr = s_julia_cr,
        .julia_ci = s_julia_ci,
    };

    u8 n = mel_job_worker_count();
    Fractal_Job_Data jd = { .compute = &fw->compute };
    atomic_store_explicit(&jd.next_row, 0, memory_order_relaxed);

    Fractal_Worker_Data wd = { .job = &jd, .row_fn = row_fn };

    Mel_Counter counter = MEL_COUNTER_INIT;
    mel_job_run_n(&wd, fractal_worker, &counter, n);

    while (mel__signal_counter(atomic_load_explicit(&counter.signal.state, memory_order_acquire)) > 0)
        SDL_CPUPauseInstruction();

    Mel_Gpu_Device* dev = mel_gpu_dev();
    Mel_Texture_Pool* pool = mel_texture_pool();

    if (fw->tex_alive)
    {
        mel_gpu_device_wait_idle(dev);
        mel_texture_pool_unload(pool, fw->tex_handle);
        fw->tex_alive = false;
    }

    mel_gpu_texture_init(&fw->tex, dev,
        .pixels = fw->pixel_buf,
        .width  = WIDTH,
        .height = HEIGHT,
        .nearest_filter = true);

    fw->tex_handle = mel_texture_pool_register(pool, &fw->tex);
    fw->tex_alive = true;
    fw->state.dirty = false;
}

static void fractal_window_init(Fractal_Window* fw, str8 title, Mel_Gpu_Device* dev)
{
    fw->window = mel_window_create(title, .width = WIDTH, .height = HEIGHT);
    fw->swapchain = mel_gpu_swapchain_create_for_window(dev, fw->window);
    Mel_Swapchain* sc = &mel_swapchain_registry_get(fw->swapchain)->swapchain;

    mel_sprite_pass_init(&fw->sprite_pass,
        .dev = dev,
        .color_format = MEL_GPU_FORMAT_B8G8R8A8_SRGB,
        .max_sprites = 4096);
    fw->sprite_pass.pool = mel_texture_pool();

    mel_render_list_init(&fw->sprite_list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    mel_render_list_init(&fw->font_list,
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .alloc = mel_alloc_heap());

    mel_render_target_init_swapchain(&fw->target, sc, dev, title);

    fw->camera = (Mel_Camera){
        .view = MEL_MAT4_IDENTITY,
        .projection = mel_mat4_ortho(0, (f32)sc->extent_width,
                                      (f32)sc->extent_height, 0, -1, 1),
    };

    fw->pixel_buf = mel_alloc(mel_alloc_heap(), (usize)(WIDTH * HEIGHT * 4));
}

static void fractal_window_shutdown(Fractal_Window* fw)
{
    if (fw->tex_alive)
    {
        mel_gpu_device_wait_idle(mel_gpu_dev());
        mel_texture_pool_unload(mel_texture_pool(), fw->tex_handle);
    }

    mel_dealloc(mel_alloc_heap(), fw->pixel_buf);
    mel_render_list_shutdown(&fw->sprite_list);
    mel_render_list_shutdown(&fw->font_list);
    mel_render_target_shutdown(&fw->target);
    mel_sprite_pass_shutdown(&fw->sprite_pass);
}

static void fractal_window_draw(Fractal_Window* fw, str8 hud_line)
{
    mel_render_list_clear(&fw->sprite_list);
    mel_render_list_clear(&fw->font_list);

    mel_draw_sprite(&fw->sprite_list,
        .pos  = mel_vec2(0.0f, 0.0f),
        .size = mel_vec2((f32)WIDTH, (f32)HEIGHT),
        .color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f),
        .tex  = fw->tex_handle);

    Mel_Vec4 white = mel_vec4(1.0f, 1.0f, 1.0f, 0.9f);
    Mel_Vec4 dim   = mel_vec4(0.6f, 0.6f, 0.6f, 0.8f);

    mel_font_atlas_draw_text(s_font_handle, &fw->font_list, hud_line, 8.0f, 4.0f, white);
    mel_font_atlas_draw_text(s_font_handle, &fw->font_list,
        S8("scroll=zoom  drag=pan  +/-=iter  R=reset  ESC=quit"),
        8.0f, 22.0f, dim);
}

static void pixel_to_complex(Fractal_State* fs, f32 px, f32 py, f64* out_r, f64* out_i)
{
    f64 aspect = (f64)WIDTH / (f64)HEIGHT;
    f64 half_h = 2.0 / fs->zoom;
    f64 half_w = half_h * aspect;
    *out_r = fs->center_x + half_w * ((f64)px / (f64)WIDTH - 0.5) * 2.0;
    *out_i = fs->center_y + half_h * ((f64)py / (f64)HEIGHT - 0.5) * 2.0;
}

static void zoom_at(Fractal_State* fs, f32 mx, f32 my, f64 factor)
{
    f64 world_x, world_y;
    pixel_to_complex(fs, mx, my, &world_x, &world_y);

    fs->zoom *= factor;

    f64 new_aspect = (f64)WIDTH / (f64)HEIGHT;
    f64 new_half_h = 2.0 / fs->zoom;
    f64 new_half_w = new_half_h * new_aspect;

    fs->center_x = world_x - new_half_w * ((f64)mx / (f64)WIDTH - 0.5) * 2.0;
    fs->center_y = world_y - new_half_h * ((f64)my / (f64)HEIGHT - 0.5) * 2.0;

    fs->dirty = true;
}

static void app_update(Mel_Sim_Ctx* sim, f32 dt, void* user);

void app_init(void)
{
    mel_vfs_mount(S8("/"), mel_vfs_backend_os(), .root = S8("/"));

    Mel_Gpu_Device* dev = mel_gpu_dev();

    s_font_handle = mel_font_atlas_load(
        .desc = mel_font_desc_load_ttf(S8("/System/Library/Fonts/Monaco.ttf")), .size = 16.0f);

    fractal_window_init(&s_mandelbrot, S8("Mandelbrot"), dev);
    fractal_window_init(&s_julia, S8("Julia Set"), dev);

    s_mandelbrot_window_id = mel_window_id(s_mandelbrot.window);
    s_julia_window_id = mel_window_id(s_julia.window);

    s_mandelbrot.state = (Fractal_State){
        .center_x = -0.5, .center_y = 0.0,
        .zoom = 1.0, .max_iter = MAX_ITER_INIT,
        .dirty = true,
    };

    s_julia.state = (Fractal_State){
        .center_x = 0.0, .center_y = 0.0,
        .zoom = 1.0, .max_iter = MAX_ITER_INIT,
        .dirty = true,
    };

    s_julia_cr = -0.7;
    s_julia_ci = 0.27015;
    s_julia_dirty = true;

    mel_render_graph_init(&s_graph, .dev = dev, .alloc = mel_alloc_heap());

    mel_render_graph_add_pass(&s_graph, S8("mandelbrot"),
        .fn = mel_sprite_pass_execute,
        .user = &s_mandelbrot.sprite_pass,
        .camera = &s_mandelbrot.camera,
        .read_lists = MEL_LISTS(&s_mandelbrot.sprite_list, &s_mandelbrot.font_list),
        .write_targets = MEL_WRITE_TARGETS(
            { .target = &s_mandelbrot.target, .load_op = MEL_GPU_LOAD_OP_CLEAR,
              .clear.color = { .r = 0.02f, .g = 0.02f, .b = 0.04f, .a = 1.0f } }));

    mel_render_graph_add_pass(&s_graph, S8("julia"),
        .fn = mel_sprite_pass_execute,
        .user = &s_julia.sprite_pass,
        .camera = &s_julia.camera,
        .read_lists = MEL_LISTS(&s_julia.sprite_list, &s_julia.font_list),
        .write_targets = MEL_WRITE_TARGETS(
            { .target = &s_julia.target, .load_op = MEL_GPU_LOAD_OP_CLEAR,
              .clear.color = { .r = 0.02f, .g = 0.02f, .b = 0.04f, .a = 1.0f } }));

    mel_render_graph_compile(&s_graph);
    mel_set_render_graph(&s_graph);

    mel_job_init();

    fractal_recompute(&s_mandelbrot, mandelbrot_row);
    fractal_recompute(&s_julia, julia_row);

    i32 threads = mel_job_worker_count();
    mel_log_info("jobs", "Fractals ready! %d worker threads + main thread", threads);
    mel_log_info("jobs", "Move mouse on Mandelbrot to change Julia c parameter");

    mel_sim_init(&s_sim, .event_buffer = s_event_buf, .event_buffer_size = sizeof(s_event_buf));
    mel_sim_add_variable(&s_sim, app_update);
    mel_register_sim(&s_sim);
}

void app_shutdown(void)
{
    mel_unregister_sim(&s_sim);
    mel_sim_shutdown(&s_sim);

    fractal_window_shutdown(&s_julia);
    fractal_window_shutdown(&s_mandelbrot);

    mel_vfs_unmount(S8("/"));
    mel_job_shutdown();
    mel_render_graph_shutdown(&s_graph);
}

static void app_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    MEL_UNUSED(sim);
    MEL_UNUSED(dt);
    MEL_UNUSED(user);

    if (s_mandelbrot.state.dirty)
        fractal_recompute(&s_mandelbrot, mandelbrot_row);

    if (s_julia.state.dirty || s_julia_dirty)
    {
        s_julia_dirty = false;
        fractal_recompute(&s_julia, julia_row);
    }

    i32 threads = mel_job_worker_count() + 1;
    char buf[256];

    snprintf(buf, sizeof(buf), "zoom: %.2e  center: (%.10f, %.10f)  iter: %d  threads: %d",
             s_mandelbrot.state.zoom, s_mandelbrot.state.center_x, s_mandelbrot.state.center_y,
             s_mandelbrot.state.max_iter, threads);
    fractal_window_draw(&s_mandelbrot, str8_from_cstr(buf));

    snprintf(buf, sizeof(buf), "c = (%.6f, %.6f)  zoom: %.2e  iter: %d",
             s_julia_cr, s_julia_ci, s_julia.state.zoom, s_julia.state.max_iter);
    fractal_window_draw(&s_julia, str8_from_cstr(buf));
}

static Fractal_State* state_for_window(u32 window_id)
{
    if (window_id == s_mandelbrot_window_id) return &s_mandelbrot.state;
    if (window_id == s_julia_window_id) return &s_julia.state;
    return nullptr;
}

void app_event(SDL_Event* event)
{
    if (event->type == SDL_EVENT_KEY_DOWN && event->key.scancode == SDL_SCANCODE_ESCAPE)
    {
        mel_quit();
        return;
    }

    if (event->type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
    {
        mel_quit();
        return;
    }

    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat)
    {
        Fractal_State* fs = state_for_window(event->key.windowID);
        if (!fs) return;

        switch (event->key.scancode)
        {
            case SDL_SCANCODE_R:
                if (fs == &s_mandelbrot.state)
                    { fs->center_x = -0.5; fs->center_y = 0.0; }
                else
                    { fs->center_x = 0.0; fs->center_y = 0.0; }
                fs->zoom = 1.0;
                fs->max_iter = MAX_ITER_INIT;
                fs->dirty = true;
                break;
            case SDL_SCANCODE_EQUALS:
            case SDL_SCANCODE_KP_PLUS:
                fs->max_iter *= 2;
                if (fs->max_iter > MAX_ITER_CAP) fs->max_iter = MAX_ITER_CAP;
                fs->dirty = true;
                break;
            case SDL_SCANCODE_MINUS:
            case SDL_SCANCODE_KP_MINUS:
                fs->max_iter /= 2;
                if (fs->max_iter < 32) fs->max_iter = 32;
                fs->dirty = true;
                break;
            default: break;
        }
    }

    if (event->type == SDL_EVENT_MOUSE_WHEEL)
    {
        Fractal_State* fs = state_for_window(event->wheel.windowID);
        if (!fs) return;

        f32 mx, my;
        SDL_GetMouseState(&mx, &my);
        f64 factor = event->wheel.y > 0 ? ZOOM_SPEED : (1.0 / ZOOM_SPEED);
        zoom_at(fs, mx, my, factor);
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT)
    {
        Fractal_State* fs = state_for_window(event->button.windowID);
        if (!fs) return;
        fs->dragging = true;
        fs->drag_start_mx = event->button.x;
        fs->drag_start_my = event->button.y;
        fs->drag_start_cx = fs->center_x;
        fs->drag_start_cy = fs->center_y;
    }

    if (event->type == SDL_EVENT_MOUSE_BUTTON_UP && event->button.button == SDL_BUTTON_LEFT)
    {
        Fractal_State* fs = state_for_window(event->button.windowID);
        if (fs) fs->dragging = false;
    }

    if (event->type == SDL_EVENT_MOUSE_MOTION)
    {
        if (event->motion.windowID == s_mandelbrot_window_id && !s_mandelbrot.state.dragging)
        {
            f64 cr, ci;
            pixel_to_complex(&s_mandelbrot.state, event->motion.x, event->motion.y, &cr, &ci);
            if (cr != s_julia_cr || ci != s_julia_ci)
            {
                s_julia_cr = cr;
                s_julia_ci = ci;
                s_julia_dirty = true;
            }
        }

        Fractal_State* fs = state_for_window(event->motion.windowID);
        if (fs && fs->dragging)
        {
            f64 aspect = (f64)WIDTH / (f64)HEIGHT;
            f64 half_h = 2.0 / fs->zoom;
            f64 half_w = half_h * aspect;

            f64 dx = (f64)(event->motion.x - fs->drag_start_mx) / (f64)WIDTH * half_w * 2.0;
            f64 dy = (f64)(event->motion.y - fs->drag_start_my) / (f64)HEIGHT * half_h * 2.0;

            fs->center_x = fs->drag_start_cx - dx;
            fs->center_y = fs->drag_start_cy - dy;
            fs->dirty = true;
        }
    }
}
