#pragma once

#include "core.types.h"
#include <SDL3/SDL.h>

int    mel_app_argc(void);
char** mel_app_argv(void);
void   mel_quit(void);
bool   mel_should_quit(void);

extern SDL_AppResult mel__app_sdl_init(int argc, char** argv);
extern SDL_AppResult mel__app_sdl_event(SDL_Event* event);
extern SDL_AppResult mel__app_sdl_iterate(void);
extern void mel__app_sdl_quit(SDL_AppResult result);
extern void mel__app_platform_init(void);
