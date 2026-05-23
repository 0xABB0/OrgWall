#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>

#include <gui/handle.h>

HWND mel_gui_win32_hwnd(Mel_Gui_Handle h);

bool mel_gui_win32_install_subclass(Mel_Gui_Handle h, SUBCLASSPROC proc,
                                    UINT_PTR id, DWORD_PTR ref);
bool mel_gui_win32_remove_subclass (Mel_Gui_Handle h, SUBCLASSPROC proc, UINT_PTR id);
