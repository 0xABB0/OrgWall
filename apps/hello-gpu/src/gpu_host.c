#include <stdlib.h>

#include <core/platform.h>
#include <gui/gui.h>
#include <string/str8.h>

#include "gpu_host.h"

typedef struct Gpu_Window
{
    const Graphical_App*   app;
    Mel_Gui_Handle         frame;
    Mel_Gui_Handle         status;
    Mel_Gui_Handle         view;
    Mel_Gpu_Surface*       surface;
    Mel_Gpu_Swapchain*     swapchain;
    Mel_Gpu_Render_Source* source;
    void*                  state;
    i32                    width, height;
    struct Gpu_Window*     next;
} Gpu_Window;

static Mel_Reactor*      g_reactor;
static Mel_Gpu_Instance* g_instance;
static Mel_Gpu_Device*   g_device;
static Gpu_Window*       g_windows;

static Gpu_Window* g_rendering;

void gpu_host_set_status(str8 text)
{
    if (g_rendering && !mel_gui_handle_is_none(g_rendering->status))
        mel_gui_set_text(g_rendering->status, text);
}

static void teardown(Gpu_Window* w);

static void gpu_host_shutdown(void)
{
    for (Gpu_Window* w = g_windows; w; w = w->next)
        teardown(w);
    if (g_device)
    {
        mel_gpu_device_destroy(g_device);
        g_device = NULL;
    }
    if (g_instance)
    {
        mel_gpu_instance_destroy(g_instance);
        g_instance = NULL;
    }
}

void gpu_host_init(Mel_Reactor* reactor)
{
    g_reactor = reactor;

    g_instance = mel_gpu_instance_create(.app_name = "hello-gpu", .debug = { .enabled = true });
    if (!g_instance)
        return;

    Mel_Gpu_Adapter* adapters[8];
    u32              n = mel_gpu_adapters(g_instance, adapters, 8);
    if (n == 0)
        return;

    Mel_Gpu_Device_Create_Result dr = mel_gpu_device_create(g_instance, adapters[0], .reactor = reactor, .features = { .timeline_semaphores = true, .descriptor_indexing = true, .buffer_device_address = true });
    g_device = dr.value;

    atexit(gpu_host_shutdown);
}

static void window_render(Mel_Gpu_Swapchain* sc, f64 dt, void* user)
{
    Gpu_Window* w = user;
    g_rendering = w;
    mel_gpu_frame_begin(sc);
    if (w->app->render)
        w->app->render(w->state, mel_gpu_frame_commands(sc), dt);
    mel_gpu_frame_end(sc);
    g_rendering = NULL;
}

static void teardown(Gpu_Window* w)
{
    if (w->source)
    {
        mel_gpu_render_source_destroy(w->source);
        w->source = NULL;
    }
    if (w->state)
    {
        if (w->app->teardown)
            w->app->teardown(w->state);
        w->state = NULL;
    }
    if (w->swapchain)
    {
        mel_gpu_swapchain_destroy(w->swapchain);
        w->swapchain = NULL;
    }
    if (w->surface)
    {
        mel_gpu_surface_destroy(w->surface);
        w->surface = NULL;
    }
}

static void window_resized(Mel_Gui_Handle h, i32 cw, i32 ch, void* user)
{
    (void)h;
    Gpu_Window* w = user;

    if (cw <= 0 || ch <= 0)
    {
        teardown(w);
        return;
    }

    w->width = cw;
    w->height = ch;

    if (w->swapchain)
    {
        mel_gpu_swapchain_resize(w->swapchain, cw, ch);
        if (w->app->resize)
            w->app->resize(w->state, cw, ch);
        return;
    }

    void* native = mel_gpu_view_surface(w->view);
    if (!native)
        return;

    w->surface = mel_gpu_surface_create(g_device, native);
    if (!w->surface)
        return;

    w->swapchain = mel_gpu_swapchain_create(g_device, .surface = w->surface, .width = cw, .height = ch, .vsync = true);
    if (!w->swapchain)
    {
        mel_gpu_surface_destroy(w->surface);
        w->surface = NULL;
        return;
    }

    w->state = w->app->init ? w->app->init(g_device, w->swapchain) : NULL;
    if (w->app->resize)
        w->app->resize(w->state, cw, ch);
    w->source = mel_gpu_render_source_new(g_reactor, w->swapchain, 60, window_render, w);
}

void gpu_host_open(const Graphical_App* app)
{
    if (!g_device)
        return;

    Gpu_Window* w = calloc(1, sizeof *w);
    w->app = app;
    w->next = g_windows;
    g_windows = w;

    w->frame = mel_frame_create(.title = str8_from_cstr(app->title), .w = 640, .h = 480);
    mel_gui_set_layout(w->frame, mel_column_layout(.spacing = 8, .margin = 12, .cross_align = MEL_ALIGN_STRETCH));

    w->status = mel_label_create(w->frame, .text = S8("Native GUI label, sharing this window with a GPU surface below."), .layoutable = { .preferred_h = 24 });

    w->view = mel_gpu_view_create(w->frame, .on_.on_resize = window_resized, .user = w, .layoutable = { .preferred_h = 400, .weight = 1 });

    mel_gui_set_bounds(w->frame, 60, 60, 640, 480);
    mel_gui_relayout(w->frame);
    mel_gui_set_visible(w->frame, true);
}
