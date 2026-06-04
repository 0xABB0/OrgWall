#pragma once

#include <hid/hid.h>

#ifdef __cplusplus
extern "C"
{
#endif

// The hidraw file descriptor behind the handle, or -1 when closed or absent. This is the fd the
// async read path lowers onto the port proactor.
int mel_hid_linux_fd(Mel_Hid_Device d);

#ifdef __cplusplus
}
#endif
