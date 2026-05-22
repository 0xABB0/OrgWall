#pragma once

#include <core/types.h>
#include <string/str8.h>

#include "handle.h"

void  mel_gui_destroy      (Mel_Gui_Handle h);
void  mel_gui_set_text     (Mel_Gui_Handle h, str8 text);
size  mel_gui_get_text     (Mel_Gui_Handle h, char* buf, size cap);
void  mel_gui_set_bounds   (Mel_Gui_Handle h, i32 x, i32 y, i32 width, i32 height);
void  mel_gui_set_visible  (Mel_Gui_Handle h, bool visible);
void  mel_gui_set_enabled  (Mel_Gui_Handle h, bool enabled);
void  mel_gui_set_focus    (Mel_Gui_Handle h);
void  mel_gui_invalidate   (Mel_Gui_Handle h);
u32   mel_gui_id           (Mel_Gui_Handle h);
void  mel_gui_set_user     (Mel_Gui_Handle h, void* user);
void* mel_gui_user         (Mel_Gui_Handle h);
void* mel_gui_native_handle(Mel_Gui_Handle h);

Mel_Gui_Handle mel_gui_focused(void);
