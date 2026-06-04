#pragma once

#include <input/input.h>

#ifdef __cplusplus
extern "C"
{
#endif

typedef struct
{
    u64  finger_id;
    f32  x, y;
    f32  pressure;
    bool active;
} Mel_Touch_Finger;

typedef struct
{
    Mel_Input_Device device;
    bool             direct;
    u32              finger_count;
} Mel_Touch_State;

Mel_Touch_State mel_touch_state(Mel_Input_Device d);
u32             mel_touch_fingers(Mel_Input_Device d, Mel_Touch_Finger* out, u32 cap);

#ifdef __cplusplus
}
#endif
