#pragma once

#include <display/display.h>

#include <X11/extensions/Xrandr.h>

#ifdef __cplusplus
extern "C" {
#endif

RROutput mel_display_x11_output(Mel_Display d);

#ifdef __cplusplus
}
#endif
