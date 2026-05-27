#include <stdlib.h>

#include <core/platform.h>
#include <gui/gui.h>
#include <string/str8.h>

#include "gpu_host.h"

typedef struct {
    const Graphical_App*   app;
    Mel_Gui_Handle         frame;
    Mel_Gui_Handle         view;
    Mel_Gpu_Swapchain*     swapchain;
    Mel_Gpu_Render_Source* source;
    void*                  state;
    i32                    width, height;
} Gpu_Window;

static Mel_Reactor*   g_reactor;
static Mel_Gpu_Device* g_device;

void gpu_host_init(Mel_Reactor* reactor)
{
    g_reactor = reactor;
    g_device  = mel_gpu_device_create(.debug = true);
}

static void window_render(Mel_Gpu_Swapchain* sc, f64 dt, void* user)
{
    Gpu_Window* w = user;
    mel_gpu_frame_begin(sc);
    if (w->app->render) w->app->render(w->state, mel_gpu_frame_commands(sc), dt);
    mel_gpu_frame_end(sc);
}

static void teardown(Gpu_Window* w)
{
    if (w->source) { mel_gpu_render_source_destroy(w->source); w->source = NULL; }
    if (w->state)  { if (w->app->teardown) w->app->teardown(w->state); w->state = NULL; }
    if (w->swapchain) { mel_gpu_swapchain_destroy(w->swapchain); w->swapchain = NULL; }
}

// The surface seam is unified across platforms: the swapchain is created on the
// first resize that has a ready surface, resized on later ones, and torn down on
// a zero size (Android's surfaceDestroyed). On macOS/web the surface is ready by
// layout time so this fires once with a valid size; on Android it follows the
// SurfaceView lifecycle.
static void window_resized(Mel_Gui_Handle h, i32 cw, i32 ch, void* user)
{
    (void)h;
    Gpu_Window* w = user;

    if (cw <= 0 || ch <= 0) { teardown(w); return; }

    w->width  = cw;
    w->height = ch;

    if (w->swapchain) {
        mel_gpu_swapchain_resize(w->swapchain, cw, ch);
        return;
    }

    void* surface = mel_gpu_view_surface(w->view);
    if (!surface) return; // surface not ready yet (Android, before surfaceCreated)

    w->swapchain = mel_gpu_swapchain_create(g_device,
        .native_window = surface, .width = cw, .height = ch, .vsync = true);
    if (!w->swapchain) return;

    w->state  = w->app->init ? w->app->init(g_device, w->swapchain) : NULL;
    w->source = mel_gpu_render_source_new(g_reactor, w->swapchain, 60, window_render, w);
}

void gpu_host_open(const Graphical_App* app)
{
    if (!g_device) return;

    Gpu_Window* w = calloc(1, sizeof *w);
    w->app = app;

    w->frame = mel_frame_create(.title = str8_from_cstr(app->title), .w = 640, .h = 480);
    mel_gui_set_layout(w->frame, mel_column_layout(
        .spacing = 8, .margin = 12, .cross_align = MEL_ALIGN_STRETCH));

    mel_label_create(w->frame,
        .text = S8("Native GUI label, sharing this window with a GPU surface below."),
        .layoutable = { .preferred_h = 24 });

    w->view = mel_gpu_view_create(w->frame,
        .on_.on_resize = window_resized,
        .user          = w,
        .layoutable    = { .preferred_h = 400, .weight = 1 });

    // Frames opened directly (not via the screen system) are not auto-sized; give
    // it a size/position and arrange children. On macOS/web this fires
    // window_resized synchronously with a ready surface, creating the swapchain;
    // on Android the swapchain waits for the SurfaceView's surface callback.
    mel_gui_set_bounds(w->frame, 60, 60, 640, 480);
    mel_gui_relayout(w->frame);
    mel_gui_set_visible(w->frame, true);
}
