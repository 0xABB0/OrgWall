#pragma once

#include <gamepad/joystick.h>

#ifdef __cplusplus
extern "C"
{
#endif

i32         mel_joystick_linux_evdev_fd(Mel_Joystick j);
const char* mel_joystick_linux_evdev_path(Mel_Joystick j);

#ifdef __cplusplus
}
#endif
