#pragma once

#include <gamepad/joystick.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef __OBJC__
@class GCController;
GCController* mel_joystick_ios_controller(Mel_Joystick j);
#endif

#ifdef __cplusplus
}
#endif
