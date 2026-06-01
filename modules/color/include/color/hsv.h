#pragma once

#include <color/rgba.h>

typedef struct mel_hsv
{
    float h, s, v;
} mel_hsv;

mel_hsv   mel_color_to_hsv(mel_color c);
mel_color mel_color_from_hsv(mel_hsv h, float a);
