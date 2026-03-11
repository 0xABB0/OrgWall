#pragma once

#include "core.types.h"

typedef struct Mugen_Stage Mugen_Stage;

typedef struct {
    f32 x, y;
    f32 bound_left, bound_right;
    f32 bound_high, bound_low;
    f32 tension;
    f32 verticalfollow;
    f32 floortension;
    f32 start_x, start_y;
} Mugen_Camera;

void mugen_camera_init(Mugen_Camera* cam, Mugen_Stage* stage);
void mugen_camera_update(Mugen_Camera* cam, f32 p1x, f32 p2x, f32 p1y, f32 p2y, f32 half_screen_w);
