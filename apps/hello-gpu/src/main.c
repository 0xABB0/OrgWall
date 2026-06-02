#include <stdlib.h>
#include <string.h>

#include <core/platform.h>
#include <app/app.h>
#include <gui/gui.h>
#include <string/str8.h>

#include "gpu_host.h"
#include "triangle.h"
#include "cube.h"
#include "lorenz.h"
#include "texquad.h"
#include "compute_plasma.h"
#include "depth3d.h"
#include "layers.h"
#include "postprocess.h"
#include "instances.h"
#include "gallery.h"

#define OPEN_BUTTON(fn, app)                     \
    static void fn(Mel_Gui_Handle h, void* user) \
    {                                            \
        (void)h;                                 \
        (void)user;                              \
        gpu_host_open(&(app));                   \
    }

OPEN_BUTTON(open_triangle_clicked, TRIANGLE_APP)
OPEN_BUTTON(open_cube_clicked, CUBE_APP)
OPEN_BUTTON(open_lorenz_clicked, LORENZ_APP)
OPEN_BUTTON(open_texquad_clicked, TEXQUAD_APP)
OPEN_BUTTON(open_plasma_clicked, COMPUTE_PLASMA_APP)
OPEN_BUTTON(open_depth_clicked, DEPTH3D_APP)
OPEN_BUTTON(open_layers_clicked, LAYERS_APP)
OPEN_BUTTON(open_post_clicked, POSTPROCESS_APP)
OPEN_BUTTON(open_instances_clicked, INSTANCES_APP)
OPEN_BUTTON(open_gallery_clicked, GALLERY_APP)

static void build_host(Mel_Gui_Handle frame, void* user)
{
    (void)user;
    mel_gui_set_text(frame, S8("Hello GPU"));
    mel_gui_set_layout(frame, mel_column_layout(.spacing = 8, .margin = 16, .cross_align = MEL_ALIGN_STRETCH));

    mel_label_create(frame, .text = S8("Native GUI window. Each button opens a new window hosting a graphical app that exercises one GPU-RHI technique."), .layoutable = { .preferred_h = 48 });

    mel_button_create(frame, .text = S8("hello-triangle"), .pointer.on_click = open_triangle_clicked, .layoutable = { .preferred_h = 36 });
    mel_button_create(frame, .text = S8("spinning-cube (CPU sort)"), .pointer.on_click = open_cube_clicked, .layoutable = { .preferred_h = 36 });
    mel_button_create(frame, .text = S8("lorenz-attractor"), .pointer.on_click = open_lorenz_clicked, .layoutable = { .preferred_h = 36 });
    mel_button_create(frame, .text = S8("bindless textured quad"), .pointer.on_click = open_texquad_clicked, .layoutable = { .preferred_h = 36 });
    mel_button_create(frame, .text = S8("compute plasma"), .pointer.on_click = open_plasma_clicked, .layoutable = { .preferred_h = 36 });
    mel_button_create(frame, .text = S8("depth-buffered 3D"), .pointer.on_click = open_depth_clicked, .layoutable = { .preferred_h = 36 });
    mel_button_create(frame, .text = S8("alpha-blended layers"), .pointer.on_click = open_layers_clicked, .layoutable = { .preferred_h = 36 });
    mel_button_create(frame, .text = S8("post-process (render-to-texture)"), .pointer.on_click = open_post_clicked, .layoutable = { .preferred_h = 36 });
    mel_button_create(frame, .text = S8("instancing (one draw)"), .pointer.on_click = open_instances_clicked, .layoutable = { .preferred_h = 36 });
    mel_button_create(frame, .text = S8("fill / blend gallery"), .pointer.on_click = open_gallery_clicked, .layoutable = { .preferred_h = 36 });
}

void mel_app_setup(Mel_Reactor* reactor)
{
    mel_gui_init(reactor);
    gpu_host_init(reactor);
    mel_app_register_screen(S8("host"), build_host, NULL);
    mel_app_present(S8("host"), NULL);

    const char* autostart = getenv("HELLO_GPU_AUTO");
    if (autostart)
    {
        if (strcmp(autostart, "cube") == 0)
            gpu_host_open(&CUBE_APP);
        else if (strcmp(autostart, "lorenz") == 0)
            gpu_host_open(&LORENZ_APP);
        else if (strcmp(autostart, "texquad") == 0)
            gpu_host_open(&TEXQUAD_APP);
        else if (strcmp(autostart, "plasma") == 0)
            gpu_host_open(&COMPUTE_PLASMA_APP);
        else if (strcmp(autostart, "depth") == 0)
            gpu_host_open(&DEPTH3D_APP);
        else if (strcmp(autostart, "layers") == 0)
            gpu_host_open(&LAYERS_APP);
        else if (strcmp(autostart, "post") == 0)
            gpu_host_open(&POSTPROCESS_APP);
        else if (strcmp(autostart, "instances") == 0)
            gpu_host_open(&INSTANCES_APP);
        else if (strcmp(autostart, "gallery") == 0)
            gpu_host_open(&GALLERY_APP);
        else
            gpu_host_open(&TRIANGLE_APP);
    }
}
