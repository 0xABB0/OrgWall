#include <SDL3/SDL.h>

#include "core.app.h"

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[])
{
    (void)appstate;
    return mel__app_sdl_init(argc, argv);
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event)
{
    (void)appstate;
    return mel__app_sdl_event(event);
}

SDL_AppResult SDL_AppIterate(void* appstate)
{
    (void)appstate;
    return mel__app_sdl_iterate();
}

void SDL_AppQuit(void* appstate, SDL_AppResult result)
{
    (void)appstate;
    mel__app_sdl_quit(result);
}
