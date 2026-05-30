#pragma once

#include <display/display.h>
#include <CoreGraphics/CGDirectDisplay.h>

#ifdef __cplusplus
extern "C" {
#endif

CGDirectDisplayID mel_display_macos_display_id(Mel_Display d);

#ifdef __OBJC__
@class NSScreen;
NSScreen* mel_display_macos_screen(Mel_Display d);
#endif

#ifdef __cplusplus
}
#endif
