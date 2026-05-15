#pragma once

#include <gui/gui.fwd.h>

bool   mel_gui_platform_init(void);
void   mel_gui_platform_shutdown(void);

void*  mel_gui_platform_create(Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc, Mel_Atom platform_class);
void   mel_gui_platform_destroy(Mel_Gui_Handle h);

bool   mel_gui_platform_set_window_pos(Mel_Gui_Handle h, i32 x, i32 y, i32 w, i32 hgt, u32 flags);
bool   mel_gui_platform_set_text(Mel_Gui_Handle h, str8 text);

bool   mel_gui_platform_post_message(Mel_Gui_Handle h, Mel_Gui_Msg msg, Mel_Gui_WParam w, Mel_Gui_LParam l);

void   mel_gui_platform_request_exit(void);

void*  mel_gui_platform_native(Mel_Gui_Handle h);
void   mel_gui_platform_bind_native(Mel_Gui_Handle h, void* native);

void   mel_gui_app_build_activity(str8 activity_name);
bool   mel_gui_app_start_activity(str8 activity_name);
