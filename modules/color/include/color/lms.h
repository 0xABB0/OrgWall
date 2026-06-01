#pragma once

#include <color/xyz.h>

typedef struct mel_lms
{
    float l, m, s;
} mel_lms;

mel_lms mel_xyz_to_lms(mel_xyz c);
mel_xyz mel_lms_to_xyz(mel_lms c);
