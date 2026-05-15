#pragma once

#include <gui/gui.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

typedef HWND (*Mel_Gui_Win32_Construct)(Mel_Gui_Handle h, const Mel_Gui_Create_Desc* desc, HWND parent_hwnd);

bool  mel_gui_win32_register_constructor(Mel_Atom atom, Mel_Gui_Win32_Construct cb);

HWND  mel_gui_win32_root(void);
HINSTANCE mel_gui_win32_hinstance(void);

void  mel_gui_win32_run(void (*setup)(void));

wchar_t* mel_gui_win32_str8_to_wide(str8 s);
void     mel_gui_win32_wide_free(wchar_t* w);
str8     mel_gui_win32_wide_to_str8(const wchar_t* w, int wlen);
void     mel_gui_win32_str8_free(str8 s);
