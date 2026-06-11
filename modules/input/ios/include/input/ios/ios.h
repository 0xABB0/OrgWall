#pragma once

#include <input/input.h>
#include <input/events.h>

#ifdef __cplusplus
extern "C"
{
#endif

#ifdef __OBJC__
@class UIEvent;
@class UIPress;
void mel_input_ios_handle_touches(const void* touches, const void* event);
void mel_input_ios_handle_presses(const void* presses, const void* event);
#endif

#ifdef __cplusplus
}
#endif
