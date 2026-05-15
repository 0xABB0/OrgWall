#pragma once

#include <gui/gui.h>

#define MEL_GUI_CLASS_SLIDER S8("mel.slider")

Mel_Atom       mel_gui_slider_atom(void);
Mel_Gui_Handle mel_gui_create_slider(Mel_Gui_Handle parent, u32 id, i32 x, i32 y, i32 w, i32 h, Mel_Gui_Proc proc, void* user);
