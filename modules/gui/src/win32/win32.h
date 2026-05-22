#pragma once

#include "../gui_internal.h"

#include <platform/win32/win32_globals.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

#define MEL_REFLECT(m) ((UINT)(WM_APP + (m)))

int            mel_gui__win32_widen (str8 s, wchar_t* buf, int cap);
size           mel_gui__win32_narrow(const wchar_t* w, int wlen, char* buf, size cap);
Mel_Gui_Handle mel_gui__win32_handle_of(HWND hwnd);
void           mel_gui__win32_bind     (HWND hwnd, Mel_Gui_Handle h);
HWND           mel_gui__win32_parent_hwnd (Mel_Gui_Widget* w);
DWORD          mel_gui__win32_child_style (Mel_Gui_Widget* w);
bool           mel_gui__win32_subclass_common(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp,
                                              Mel_Gui_Handle h);
