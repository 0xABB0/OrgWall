#pragma once

#include <hid/hid.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The Win32 file HANDLE opened on the HID device interface, or INVALID_HANDLE_VALUE when closed or
// absent. Opened with FILE_FLAG_OVERLAPPED so the async read path can ride overlapped I/O.
HANDLE mel_hid_win32_handle(Mel_Hid_Device d);

#ifdef __cplusplus
}
#endif
