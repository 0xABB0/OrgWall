#include "core.app.h"
#include "core.engine.h"

__attribute__((weak)) void app_init(void) { assert(false); }
__attribute__((weak)) void app_shutdown(void) {}
__attribute__((weak)) void app_event(SDL_Event* event) { (void)event; }

static int s_argc;
static char** s_argv;
static bool s_shutdown_called;
static bool s_init_completed;
static bool s_should_quit;

int mel_app_argc(void)
{
    return s_argc;
}

char** mel_app_argv(void)
{
    return s_argv;
}

void mel_quit(void)
{
    s_should_quit = true;
}

bool mel_should_quit(void)
{
    return s_should_quit;
}

SDL_AppResult mel__app_sdl_init(int argc, char** argv)
{
    s_argc = argc;
    s_argv = argv;
    s_shutdown_called = false;
    s_init_completed = false;
    s_should_quit = false;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        return SDL_APP_FAILURE;

    mel__app_platform_init();

    mel_boot();

    s_init_completed = true;
    return SDL_APP_CONTINUE;
}

SDL_AppResult mel__app_sdl_event(SDL_Event* event)
{
    if (event->type == SDL_EVENT_QUIT) {
        s_should_quit = true;
        return SDL_APP_SUCCESS;
    }

    mel_process_event(event);
    app_event(event);
    return SDL_APP_CONTINUE;
}

SDL_AppResult mel__app_sdl_iterate(void)
{
    if (s_should_quit)
        return SDL_APP_SUCCESS;

    mel_frame();

    return SDL_APP_CONTINUE;
}

void mel__app_sdl_quit(SDL_AppResult result)
{
    (void)result;

    if (s_shutdown_called)
        return;
    s_shutdown_called = true;

    if (s_init_completed)
        app_shutdown();

    mel_shutdown();

    SDL_Quit();
}
