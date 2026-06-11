#pragma once

#include <gamepad/joystick.h>

#ifdef __cplusplus
extern "C"
{
#endif

u32   mel_joystick_win32_xinput_index(Mel_Joystick j);
void* mel_joystick_win32_rawinput_handle(Mel_Joystick j);

#ifdef __cplusplus
}
#endif
