#pragma once

#include <color/rgba.h>

typedef struct mel_hsl {
    float h, s, l;
} mel_hsl;

mel_hsl mel_color_to_hsl(mel_color c);
mel_color mel_color_from_hsl(mel_hsl h, float a);
