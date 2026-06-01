#pragma once

#include <string/str8.h>

#include "handle.h"

typedef void (*Mel_Screen_Build)(Mel_Gui_Handle frame, void* arg);

/* Screen registration follows the _opt pattern. `build` and `user` are positional
 * by convention (first two fields), so mel_app_register_screen(name, build, user)
 * still works; lifecycle hooks are designated fields. on_enter fires whenever an
 * instance becomes the visible top (first show and every re-reveal), on_leave when
 * it stops being top, on_destroy just before its frame is torn down. The arg
 * matches what the builder received for that instance. */
typedef struct
{
    Mel_Screen_Build build;
    void*            user;
    void (*on_enter)(Mel_Gui_Handle frame, void* arg);
    void (*on_leave)(Mel_Gui_Handle frame, void* arg);
    void (*on_destroy)(Mel_Gui_Handle frame, void* arg);
} Mel_Screen_Opt;

void mel_app_register_screen_opt(str8 name, Mel_Screen_Opt opt);
#define mel_app_register_screen(name, ...) mel_app_register_screen_opt((name), (Mel_Screen_Opt){ __VA_ARGS__ })

/* Open a new Root (top-level surface) with its own Navigator rooted at `name`.
 * Desktop: a new window. The multi-Root verb; its mobile/web shape lands later. */
void mel_app_present(str8 name, void* arg);

/* Within the Navigator that owns `from`'s Root: */
void mel_app_push(Mel_Gui_Handle from, str8 name, void* arg);    /* predecessor hidden; back returns */
void mel_app_replace(Mel_Gui_Handle from, str8 name, void* arg); /* predecessor destroyed */
void mel_app_back(Mel_Gui_Handle from);                          /* pop top, reveal predecessor */
void mel_app_pop_to(Mel_Gui_Handle from, str8 name);
void mel_app_pop_to_root(Mel_Gui_Handle from);
