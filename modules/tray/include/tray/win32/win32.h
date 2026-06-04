#pragma once

#include <tray/tray.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

HWND  mel_tray_win32_message_window(Mel_Tray t);
HMENU mel_tray_win32_menu(Mel_Tray t);
UINT  mel_tray_win32_icon_id(Mel_Tray t);
#endif
