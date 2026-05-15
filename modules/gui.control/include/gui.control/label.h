#pragma once

#include <gui/gui.h>

#define MEL_GUI_CLASS_LABEL S8("mel.label")

Mel_Atom       mel_gui_label_atom(void);
Mel_Gui_Handle mel_gui_create_label(Mel_Gui_Handle parent, str8 text, u32 id, i32 x, i32 y, i32 w, i32 h);
