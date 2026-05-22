#pragma once

#include <string/str8.h>

#include "handle.h"

typedef void (*Mel_Screen_Build)(Mel_Gui_Handle frame, void* user);

void mel_app_register_screen(str8 name, Mel_Screen_Build build, void* user);
void mel_app_present(str8 name);
