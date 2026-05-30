#pragma once

#include <display/display.h>

#ifdef __OBJC__
@class UIScreen;
UIScreen* mel_display_ios_screen(Mel_Display d);
#endif
