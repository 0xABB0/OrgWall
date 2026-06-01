#pragma once

#include <color/rgba.h>
#include <color/xyz.h>

typedef struct mel_lab
{
    float l, a, b;
} mel_lab;

typedef struct mel_lch
{
    float l, c, h;
} mel_lch;

mel_lab mel_xyz_to_lab(mel_xyz c, mel_xyz white);
mel_xyz mel_lab_to_xyz(mel_lab c, mel_xyz white);

mel_lch mel_lab_to_lch(mel_lab c);
mel_lab mel_lch_to_lab(mel_lch c);

mel_lab   mel_color_to_lab(mel_color c);
mel_color mel_color_from_lab(mel_lab c, float a);

mel_lch   mel_color_to_lch(mel_color c);
mel_color mel_color_from_lch(mel_lch c, float a);
