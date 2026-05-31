#pragma once

#include <color/rgba.h>

typedef struct mel_oklab {
    float l, a, b;
} mel_oklab;

mel_oklab mel_color_to_oklab(mel_color c);
mel_color mel_color_from_oklab(mel_oklab o, float a);
