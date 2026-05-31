#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct mel_color {
    float r, g, b, a;
} mel_color;

mel_color mel_color_rgb(float r, float g, float b);
mel_color mel_color_rgba(float r, float g, float b, float a);
mel_color mel_color_gray(float v);

float mel_color_srgb_to_linear(float c);
float mel_color_linear_to_srgb(float c);

mel_color mel_color_with_alpha(mel_color c, float a);
mel_color mel_color_scale(mel_color c, float s);
mel_color mel_color_clamp01(mel_color c);

mel_color mel_color_lerp(mel_color a, mel_color b, float t);
mel_color mel_color_mix(mel_color a, mel_color b, float t);
mel_color mel_color_over(mel_color src, mel_color dst);

float mel_color_luminance(mel_color c);
float mel_color_contrast(mel_color a, mel_color b);

mel_color mel_color_grayscale(mel_color c);
mel_color mel_color_lighten(mel_color c, float amount);
mel_color mel_color_darken(mel_color c, float amount);
mel_color mel_color_saturate(mel_color c, float amount);
mel_color mel_color_desaturate(mel_color c, float amount);
