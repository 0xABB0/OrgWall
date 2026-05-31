#pragma once

#include <color/rgba.h>

typedef struct mel_hwb {
    float h, w, b;
} mel_hwb;

mel_hwb mel_color_to_hwb(mel_color c);
mel_color mel_color_from_hwb(mel_hwb c, float a);
