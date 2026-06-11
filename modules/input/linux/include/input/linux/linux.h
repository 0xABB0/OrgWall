#pragma once

#include <input/input.h>

#ifdef __cplusplus
extern "C"
{
#endif

int  mel_input_linux_fd(Mel_Input_Device d);
void mel_input_linux_set_wayland(void* wl_seat);

#ifdef __cplusplus
}
#endif
