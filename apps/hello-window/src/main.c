#include <reactor/reactor.h>
#include <string/str8.h>
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

static bool app_init(Mel_Reactor* reactor, void* user)
{
    (void)user;
    mel_window_init(reactor);
    mel_window_create(.title = S8("hello window"),
                      .w = 640,
                      .h = 480,
                      .lifecycle = { .on_resize = on_resize, .on_closed = on_closed },
                      .display = { .on_scale_changed = on_scale_changed },
                      .app = { .on_occluded = on_occluded, .on_foreground = on_foreground },
                      .input = { .on_pointer_move = on_pointer_move, .on_key_down = on_key_down });
    return true;
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;
    return mel_reactor_spawn(MEL_REACTOR_THREADED, app_init, NULL);
}
