#pragma once

#include <camera/camera.h>

#ifdef __cplusplus
extern "C"
{
#endif

bool mel_camera_android_permission_granted(void);
bool mel_camera_android_request_permission(void);
void mel_camera_android_on_permission(bool granted);

#ifdef __cplusplus
}
#endif
