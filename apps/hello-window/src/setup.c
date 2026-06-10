#include <boot/boot.h>
#include <string/str8.h>
#include <vat/vat.h>
#include <window/window.h>

static void on_resize(Mel_Window w, i32 pixel_w, i32 pixel_h, void* user)
{
    (void)w;
    (void)pixel_w;
    (void)pixel_h;
    (void)user;
}

static void on_closed(Mel_Window w, void* user)
{
    (void)w;
    (void)user;
}

static void on_scale_changed(Mel_Window w, f32 scale, void* user)
{
    (void)w;
    (void)scale;
    (void)user;
}

static void on_pointer_move(Mel_Window w, i32 x, i32 y, void* user)
{
    (void)w;
    (void)x;
    (void)y;
    (void)user;
}

static void on_key_down(Mel_Window w, u32 key, void* user)
{
    (void)w;
    (void)key;
    (void)user;
}

static void on_occluded(Mel_Window w, bool occluded, void* user)
{
    (void)w;
    (void)occluded;
    (void)user;
}

static void on_foreground(Mel_Window w, void* user)
{
    (void)w;
    (void)user;
}

static void quit_vat(void* user) { mel_vat_quit((Mel_Vat*)user); }

static void shutdown_window(void* user)
{
    (void)user;
    mel_window_shutdown();
}

void mel_app_setup(Mel_Vat* root)
{
    mel_window_init((Mel_Window_Host){ .quit = quit_vat, .user = root });
    mel_app_on_exit(shutdown_window, NULL);
    mel_window_create(.title = S8("hello window"),
                      .w = 640,
                      .h = 480,
                      .lifecycle = { .on_resize = on_resize, .on_closed = on_closed },
                      .display = { .on_scale_changed = on_scale_changed },
                      .app = { .on_occluded = on_occluded, .on_foreground = on_foreground },
                      .input = { .on_pointer_move = on_pointer_move, .on_key_down = on_key_down });
    mel_vat_retain(root);
}
