#pragma once

#include <core/types.h>
#include <reactor/reactor.h>

typedef enum {
    MEL_GUI_CAP_MULTI_WINDOW = 1,
    MEL_GUI_CAP_NATIVE_MENUS,
} Mel_Gui_Capability;

void mel_gui_init(Mel_Reactor* reactor);
bool mel_gui_backend_supports(Mel_Gui_Capability cap);
