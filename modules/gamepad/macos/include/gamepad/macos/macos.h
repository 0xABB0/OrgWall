#pragma once

#include <gamepad/joystick.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef __OBJC__
@class GCController;
GCController* mel_joystick_macos_controller(Mel_Joystick j);
#endif

void* mel_joystick_macos_iohid_device(Mel_Joystick j);

#ifdef __cplusplus
}
#endif
