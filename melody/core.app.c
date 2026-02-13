#include "core.app.h"
#include "debug.backtrace.h"

void mel__engine_init(void)
{
    mel_backtrace_init();
}

void mel__engine_shutdown(void)
{
}

__attribute__((weak)) void mel_engine_process_event(Mel_Engine* engine, SDL_Event* event)
{
    (void)engine; (void)event;
}

__attribute__((weak)) void mel_engine_frame(Mel_Engine* engine, Mel_App* app)
{
    (void)engine; (void)app;
}

__attribute__((weak)) void mel_engine_shutdown(Mel_Engine* engine)
{
    (void)engine;
}

SDL_AppResult mel__app_sdl_init(Mel_App* app, Mel_App_Opt* opt, void** appstate)
{
    *app = (Mel_App){0};
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

    if (app->engine.window)
        mel_engine_process_event(&app->engine, event);

    if (app->opt.on_event)
        app->opt.on_event(app, event);
    return SDL_APP_CONTINUE;
}

SDL_AppResult mel__app_sdl_iterate(Mel_App* app)
{
    if (app->should_quit)
        return SDL_APP_SUCCESS;

    if (app->engine.window)
    {
        mel_engine_frame(&app->engine, app);
    }
    else
    {
        if (app->opt.on_update)
            app->opt.on_update(app, 0);
    }

    return SDL_APP_CONTINUE;
}

void mel__app_sdl_quit(Mel_App* app, SDL_AppResult result)
{
    (void)result;

    SDL_Window* window = app ? app->engine.window : nullptr;

    if (app && app->opt.on_shutdown)
        app->opt.on_shutdown(app);

    if (app && window)
        mel_engine_shutdown(&app->engine);

    if (window)
        SDL_DestroyWindow(window);

    mel__engine_shutdown();
    SDL_Quit();
}
