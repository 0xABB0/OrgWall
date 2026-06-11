#pragma once

#include <hid/hid.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The dup'd UsbDeviceConnection file descriptor behind the handle (from UsbManager.openDevice via
// JNI), or -1 when closed or absent. Bluetooth HID devices route through the Java HID profile and
// surface no fd here.
int mel_hid_android_fd(Mel_Hid_Device d);

#ifdef __cplusplus
}
#endif
