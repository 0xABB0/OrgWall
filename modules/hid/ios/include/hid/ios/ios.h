#pragma once

#include <hid/hid.h>

#ifdef __cplusplus
extern "C"
{
#endif

// iOS does not expose the public IOKit HID interface (IOHIDManager) to third-party apps: raw HID
// enumeration is not a capability the platform grants. This module therefore registers no HID
// transport on iOS (honest absence, MEL-ENGINE-VII); mel_hid_count() is 0 and this accessor returns
// NULL. HID-class input on iOS arrives through GameController.framework / ExternalAccessory for the
// accessory classes the platform permits; a future bridge over those would surface its native object
// here.
void* mel_hid_ios_device(Mel_Hid_Device d);

#ifdef __cplusplus
}
#endif
