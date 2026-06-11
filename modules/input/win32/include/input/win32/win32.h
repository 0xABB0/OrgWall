#pragma once

#include <input/input.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct HWND__ HWND__;

void mel_input_win32_set_hwnd(void* hwnd);
i64  mel_input_win32_wndproc(void* hwnd, u32 msg, u64 wparam, i64 lparam, bool* handled);

#ifdef __cplusplus
}
#endif
