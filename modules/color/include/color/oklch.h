#pragma once

#include <color/oklab.h>
#include <color/rgba.h>

typedef struct mel_oklch
{
    float l, c, h;
} mel_oklch;

mel_oklch mel_color_to_oklch(mel_color c);
mel_color mel_color_from_oklch(mel_oklch o, float a);
