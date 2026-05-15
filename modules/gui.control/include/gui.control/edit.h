#pragma once

#include <gui/gui.h>

#define MEL_GUI_CLASS_EDIT S8("mel.edit")

Mel_Atom       mel_gui_edit_atom(void);
Mel_Gui_Handle mel_gui_create_edit(Mel_Gui_Handle parent, str8 text, u32 id, i32 x, i32 y, i32 w, i32 h, Mel_Gui_Proc proc, void* user);
