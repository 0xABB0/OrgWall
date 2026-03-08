#include "core.app.h"
#include "core.engine.h"
#include "debug.backtrace.h"

SDL_AppResult mel__app_sdl_init(Mel_App* app, Mel_App_Opt* opt, void** appstate)
{
    int saved_argc = app->argc;
    char** saved_argv = app->argv;
    *app = (Mel_App){0};
    app->argc = saved_argc;
    app->argv = saved_argv;
    app->opt = *opt;

    mel__engine_init();

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        return SDL_APP_FAILURE;

    mel__app_platform_init();

    if (app->opt.on_init)
        app->opt.on_init(app);

    *appstate = app;
    return SDL_APP_CONTINUE;
}

SDL_AppResult mel__app_sdl_event(Mel_App* app, SDL_Event* event)
{
    if (event->type == SDL_EVENT_QUIT) {
        app->should_quit = true;
        return SDL_APP_SUCCESS;
    }

    mel_process_event(event);

    if (app->opt.on_event)
        app->opt.on_event(app, event);
    return SDL_APP_CONTINUE;
}

SDL_AppResult mel__app_sdl_iterate(Mel_App* app)
{
    if (app->should_quit)
        return SDL_APP_SUCCESS;

    mel_frame();

    return SDL_APP_CONTINUE;
}

void mel__app_sdl_quit(Mel_App* app, SDL_AppResult result)
{
    (void)result;

    if (app && app->opt.on_shutdown)
        app->opt.on_shutdown(app);

    mel_shutdown();

    mel__engine_shutdown();
    SDL_Quit();
}
