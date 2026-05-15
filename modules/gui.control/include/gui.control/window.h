#pragma once

#include <gui/gui.h>

#define MEL_GUI_CLASS_WINDOW S8("mel.window")

Mel_Atom       mel_gui_window_atom(void);
Mel_Gui_Handle mel_gui_create_window(str8 title, i32 w, i32 h, Mel_Gui_Proc proc, void* user);
