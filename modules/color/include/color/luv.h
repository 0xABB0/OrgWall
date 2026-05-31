#pragma once

#include <color/rgba.h>
#include <color/xyz.h>

typedef struct mel_luv {
    float l, u, v;
} mel_luv;

typedef struct mel_lchuv {
    float l, c, h;
} mel_lchuv;

mel_luv mel_xyz_to_luv(mel_xyz c, mel_xyz white);
mel_xyz mel_luv_to_xyz(mel_luv c, mel_xyz white);

mel_lchuv mel_luv_to_lchuv(mel_luv c);
mel_luv mel_lchuv_to_luv(mel_lchuv c);

mel_luv mel_color_to_luv(mel_color c);
mel_color mel_color_from_luv(mel_luv c, float a);
