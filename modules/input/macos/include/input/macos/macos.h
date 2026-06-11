#pragma once

#include <input/input.h>
#include <input/events.h>

#ifdef __cplusplus
extern "C"
{
#endif

void mel_input_macos_handle_nsevent(const void* nsevent);

#ifdef __OBJC__
@class NSCursor;
NSCursor* mel_input_macos_cursor(Mel_Cursor c);
#endif

#ifdef __cplusplus
}
#endif
