#pragma once

#include <hid/hid.h>

#include <IOKit/hid/IOHIDDevice.h>

#ifdef __cplusplus
extern "C"
{
#endif

// Live IOHIDDeviceRef behind the handle, or NULL when the device is closed or removed.
IOHIDDeviceRef mel_hid_macos_device(Mel_Hid_Device d);

#ifdef __cplusplus
}
#endif
