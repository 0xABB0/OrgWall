#pragma once

#include <input/input.h>

#ifdef __cplusplus
extern "C"
{
#endif

void mel_input_android_handle_key(i32 action, i32 keycode, i32 meta_state, i32 unicode, bool repeat);
void mel_input_android_handle_motion(i32 source, i32 action, i32 pointer_id, f32 x, f32 y, f32 pressure);

#ifdef __cplusplus
}
#endif
