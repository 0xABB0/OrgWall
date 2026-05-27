#include <core/platform.h>
#include <app/app.h>
#include <gui/gui.h>
#include <string/str8.h>

#include "gpu_host.h"
#include "triangle.h"
#include "cube.h"
#include "lorenz.h"

static void open_triangle_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    gpu_host_open(&TRIANGLE_APP);
}

static void open_cube_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    gpu_host_open(&CUBE_APP);
}

static void open_lorenz_clicked(Mel_Gui_Handle h, void* user)
{
    (void)h;
    (void)user;
    gpu_host_open(&LORENZ_APP);
}

static void build_host(Mel_Gui_Handle frame, void* user)
{
    (void)user;
    mel_gui_set_text(frame, S8("Hello GPU"));
    mel_gui_set_layout(frame, mel_column_layout(
        .spacing = 10, .margin = 20, .cross_align = MEL_ALIGN_STRETCH));

    mel_label_create(frame,
        .text = S8("Native GUI window. Each button opens a new window hosting a graphical app."),
        .layoutable = { .preferred_h = 48 });

    mel_button_create(frame,
        .text = S8("Open hello-triangle"),
        .pointer.on_click = open_triangle_clicked,
        .layoutable = { .preferred_h = 44 });

    mel_button_create(frame,
        .text = S8("Open spinning-cube"),
        .pointer.on_click = open_cube_clicked,
        .layoutable = { .preferred_h = 44 });

    mel_button_create(frame,
        .text = S8("Open lorenz-attractor"),
        .pointer.on_click = open_lorenz_clicked,
        .layoutable = { .preferred_h = 44 });
}

void mel_app_setup(Mel_Reactor* reactor)
{
    mel_gui_init(reactor);
    gpu_host_init(reactor);
    mel_app_register_screen(S8("host"), build_host, NULL);
    mel_app_present(S8("host"));
}
