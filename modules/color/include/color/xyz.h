#pragma once

#include <color/rgba.h>

typedef struct mel_xyz
{
    float x, y, z;
} mel_xyz;

typedef struct mel_xyy
{
    float x, y, Y;
} mel_xyy;

mel_xyz mel_xyz_make(float x, float y, float z);

mel_xyz   mel_color_to_xyz(mel_color c);
mel_color mel_color_from_xyz(mel_xyz c, float a);

mel_xyy mel_xyz_to_xyy(mel_xyz c);
mel_xyz mel_xyy_to_xyz(mel_xyy c);
