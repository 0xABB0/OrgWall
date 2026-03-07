#pragma once

#include "core.types.h"
#include "core.engine.h"
#include <SDL3/SDL.h>

typedef struct Mel_App Mel_App;

typedef void (*Mel_App_Init_Func)(Mel_App* app);
typedef void (*Mel_App_Shutdown_Func)(Mel_App* app);
typedef void (*Mel_App_Event_Func)(Mel_App* app, SDL_Event* event);

typedef struct {
    Mel_App_Init_Func on_init;
    Mel_App_Shutdown_Func on_shutdown;
    Mel_App_Event_Func on_event;
} Mel_App_Opt;

struct Mel_App {
    Mel_App_Opt opt;
    Mel_Engine engine;
    bool should_quit;
    int argc;
    char** argv;
};

extern SDL_AppResult mel__app_sdl_init(Mel_App* app, Mel_App_Opt* opt, void** appstate);
extern SDL_AppResult mel__app_sdl_event(Mel_App* app, SDL_Event* event);
extern SDL_AppResult mel__app_sdl_iterate(Mel_App* app);
extern void mel__app_sdl_quit(Mel_App* app, SDL_AppResult result);
extern void mel__app_platform_init(void);

#define MEL_APP(...)                                                    \
    static Mel_App s_mel_app;                                           \
    static Mel_App_Opt s_mel_app_opt = (Mel_App_Opt){__VA_ARGS__};      \
                                                                        \
    SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) { \
        s_mel_app.argc = argc;                                          \
        s_mel_app.argv = argv;                                          \
        return mel__app_sdl_init(&s_mel_app, &s_mel_app_opt, appstate); \
    }                                                                   \
    SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {      \
        return mel__app_sdl_event((Mel_App*)appstate, event);           \
    }                                                                   \
    SDL_AppResult SDL_AppIterate(void* appstate) {                      \
        return mel__app_sdl_iterate((Mel_App*)appstate);                \
    }                                                                   \
    void SDL_AppQuit(void* appstate, SDL_AppResult result) {            \
        mel__app_sdl_quit((Mel_App*)appstate, result);                  \
    }
