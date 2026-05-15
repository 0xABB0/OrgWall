#pragma once

#include "gui.h"

struct Mel_Gui_Handle__ {
    void* platform_handle;
    Mel_Gui_Handle parent;
    u32 id;
    Mel_Gui_Atom class_atom;
    Mel_Gui_Proc proc;
    void* user;
};

bool mel_gui_platform_init(void);
void mel_gui_platform_shutdown(void);
bool mel_gui_platform_realize(Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc, str8 platform_class_name);
void mel_gui_platform_destroy(Mel_Gui_Handle h);
void mel_gui_platform_show(Mel_Gui_Handle h, bool visible);
void mel_gui_platform_enable(Mel_Gui_Handle h, bool enabled);
void mel_gui_platform_set_text(Mel_Gui_Handle h, str8 text);
void mel_gui_platform_set_rect(Mel_Gui_Handle h, i32 x, i32 y, i32 width, i32 height);

void mel_gui__set_platform_handle(Mel_Gui_Handle h, void* platform_handle);
void* mel_gui__platform_handle(Mel_Gui_Handle h);
