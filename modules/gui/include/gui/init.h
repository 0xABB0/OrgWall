#pragma once

#include <core/types.h>
#include <vat/vat.h>

typedef enum
{
    MEL_GUI_CAP_MULTI_WINDOW = 1,
    MEL_GUI_CAP_NATIVE_MENUS,
} Mel_Gui_Capability;

void mel_gui_init(Mel_Vat* vat);
void mel_gui_shutdown(void);
bool mel_gui_backend_supports(Mel_Gui_Capability cap);

/* True where the backend hosts multiple coequal top-level Roots (desktop windows,
 * XR panels). False on single-surface backends (phone, web document), where
 * mel_app_present degrades to a push on the one Root. Fixed per backend. */
bool mel_gui_supports_multi_root(void);
