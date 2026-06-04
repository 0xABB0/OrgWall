#pragma once

#include <input/input.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define MEL_PEN_BUTTON_MAX 5u

typedef struct
{
    Mel_Input_Device device;
    f32              x, y;
    f32              pressure;
    f32              tilt_x, tilt_y;
    f32              distance;
    f32              rotation;
    f32              tangential_pressure;
    f32              slider;
    u32              buttons;
    bool             eraser;
    bool             in_proximity;
} Mel_Pen_State;

Mel_Pen_State mel_pen_state(Mel_Input_Device d);

#ifdef __cplusplus
}
#endif
