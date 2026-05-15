#pragma once

#include <gui/gui.h>

#define MEL_GUI_CLASS_PANEL S8("mel.panel")

Mel_Atom       mel_gui_panel_atom(void);
Mel_Gui_Handle mel_gui_create_panel(Mel_Gui_Handle parent, u32 id, i32 x, i32 y, i32 w, i32 h, Mel_Gui_Proc proc, void* user);
