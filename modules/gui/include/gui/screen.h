#pragma once

#include <string/str8.h>

#include "handle.h"

typedef void (*Mel_Screen_Build)(Mel_Gui_Handle frame, void* arg);

void mel_app_register_screen(str8 name, Mel_Screen_Build build, void* user);

/* Open a new Root (top-level surface) with its own Navigator rooted at `name`.
 * Desktop: a new window. The multi-Root verb; its mobile/web shape lands later. */
void mel_app_present(str8 name, void* arg);

/* Within the Navigator that owns `from`'s Root: */
void mel_app_push       (Mel_Gui_Handle from, str8 name, void* arg); /* predecessor hidden; back returns */
void mel_app_replace    (Mel_Gui_Handle from, str8 name, void* arg); /* predecessor destroyed */
void mel_app_back       (Mel_Gui_Handle from);                        /* pop top, reveal predecessor */
void mel_app_pop_to     (Mel_Gui_Handle from, str8 name);
void mel_app_pop_to_root(Mel_Gui_Handle from);
