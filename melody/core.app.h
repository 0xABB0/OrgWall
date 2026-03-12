#pragma once

#include "core.types.h"
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
    bool should_quit;
    int argc;
    char** argv;
};

int    mel_app_argc(void);
char** mel_app_argv(void);
void   mel_quit(void);
bool   mel_should_quit(void);

extern SDL_AppResult mel__app_sdl_init(int argc, char** argv);
extern SDL_AppResult mel__app_sdl_event(SDL_Event* event);
extern SDL_AppResult mel__app_sdl_iterate(void);
extern void mel__app_sdl_quit(SDL_AppResult result);
extern void mel__app_platform_init(void);

#define MEL_APP(...)                                                     \
    static Mel_App_Opt s_mel_app_opt = (Mel_App_Opt){__VA_ARGS__};      \
    int mel__legacy_app_present = 1;                                     \
                                                                         \
    void mel__legacy_app_init(Mel_App* app)                              \
    {                                                                    \
        if (s_mel_app_opt.on_init) s_mel_app_opt.on_init(app);           \
    }                                                                    \
                                                                         \
    void mel__legacy_app_shutdown(Mel_App* app)                          \
    {                                                                    \
        if (s_mel_app_opt.on_shutdown) s_mel_app_opt.on_shutdown(app);   \
    }                                                                    \
                                                                         \
    void mel__legacy_app_event(Mel_App* app, SDL_Event* event)           \
    {                                                                    \
        if (s_mel_app_opt.on_event) s_mel_app_opt.on_event(app, event);  \
    }
