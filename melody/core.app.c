#include "core.app.h"
#include "core.engine.h"
#include "debug.backtrace.h"
#include "string.str8.h"

__attribute__((weak)) Mel_App_Config app_config(void)
{
    return (Mel_App_Config){0};
}

__attribute__((weak)) void app_init(void)
{
    assert(false);
}

__attribute__((weak)) void app_shutdown(void) {}

__attribute__((weak)) void app_event(SDL_Event* event)
{
    (void)event;
}

__attribute__((weak)) int mel__legacy_app_present;
__attribute__((weak)) void mel__legacy_app_init(Mel_App* app) { (void)app; }
__attribute__((weak)) void mel__legacy_app_shutdown(Mel_App* app) { (void)app; }
__attribute__((weak)) void mel__legacy_app_event(Mel_App* app, SDL_Event* event)
{
    (void)app;
    (void)event;
}

static Mel_App s_app;
static bool s_app_shutdown_called;
static bool s_app_init_completed;

int mel_app_argc(void)
{
    return s_app.argc;
}

char** mel_app_argv(void)
{
    return s_app.argv;
}

void mel_quit(void)
{
    s_app.should_quit = true;
}

bool mel_should_quit(void)
{
    return s_app.should_quit;
}

SDL_AppResult mel__app_sdl_init(int argc, char** argv)
{
    s_app = (Mel_App){
        .argc = argc,
        .argv = argv,
    };
    s_app_shutdown_called = false;
    s_app_init_completed = false;

    mel__engine_init();

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        return SDL_APP_FAILURE;

    mel__app_platform_init();

    if (!mel__legacy_app_present)
    {
        Mel_App_Config config = app_config();
        str8 app_name = config.app_name;
        if (str8_is_empty(app_name) && argc > 0 && argv && argv[0])
            app_name = str8_from_cstr(argv[0]);

        if (!mel_init(
            .app_name = app_name,
            .allocator = config.allocator,
            .enable_validation = config.enable_validation,
            .max_frame_time = config.max_frame_time))
            return SDL_APP_FAILURE;
    }

    if (mel__legacy_app_present)
        mel__legacy_app_init(&s_app);
    else
        app_init();

    s_app_init_completed = true;

    return SDL_APP_CONTINUE;
}

SDL_AppResult mel__app_sdl_event(SDL_Event* event)
{
    if (event->type == SDL_EVENT_QUIT) {
        s_app.should_quit = true;
        return SDL_APP_SUCCESS;
    }

    mel_process_event(event);

    if (mel__legacy_app_present)
        mel__legacy_app_event(&s_app, event);
    else
        app_event(event);
    return SDL_APP_CONTINUE;
}

SDL_AppResult mel__app_sdl_iterate(void)
{
    if (s_app.should_quit)
        return SDL_APP_SUCCESS;

    mel_frame();

    return SDL_APP_CONTINUE;
}

void mel__app_sdl_quit(SDL_AppResult result)
{
    (void)result;

    if (s_app_shutdown_called)
        return;
    s_app_shutdown_called = true;

    if (s_app_init_completed)
    {
        if (mel__legacy_app_present)
            mel__legacy_app_shutdown(&s_app);
        else
            app_shutdown();
    }

    mel_shutdown();

    mel__engine_shutdown();
    SDL_Quit();
}
