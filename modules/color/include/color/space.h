#pragma once

#include <color/rgba.h>
#include <color/rgba8.h>
#include <color/xyz.h>

typedef struct mel_chromaticity {
    float x, y;
} mel_chromaticity;

typedef struct mel_white_point {
    float x, y;
} mel_white_point;

mel_white_point mel_white_d65(void);
mel_white_point mel_white_d50(void);
mel_white_point mel_white_aces(void);
mel_white_point mel_white_e(void);

mel_xyz mel_white_point_xyz(mel_white_point w);

typedef struct mel_color_space {
    mel_chromaticity red, green, blue;
    mel_white_point white;
    float (*to_linear)(float c);
    float (*to_encoded)(float c);
} mel_color_space;

mel_color_space mel_color_space_srgb(void);
mel_color_space mel_color_space_linear_srgb(void);
mel_color_space mel_color_space_display_p3(void);
mel_color_space mel_color_space_rec2020(void);
mel_color_space mel_color_space_rec2020_pq(void);
mel_color_space mel_color_space_rec2020_hlg(void);
mel_color_space mel_color_space_adobe_rgb(void);
mel_color_space mel_color_space_prophoto(void);
mel_color_space mel_color_space_aces_cg(void);
mel_color_space mel_color_space_aces2065_1(void);

mel_xyz mel_linear_rgb_to_xyz(mel_color linear, const mel_color_space *s);
mel_color mel_xyz_to_linear_rgb(mel_xyz c, const mel_color_space *s, float a);

mel_color mel_color_convert(mel_color c, const mel_color_space *from, const mel_color_space *to);

mel_color8 mel_color_to_8_in(mel_color linear, const mel_color_space *s);
mel_color mel_color_from_8_in(mel_color8 enc, const mel_color_space *s);

typedef struct mel_p3 {
    float r, g, b, a;
} mel_p3;

typedef struct mel_rec2020 {
    float r, g, b, a;
} mel_rec2020;

typedef struct mel_aces_cg {
    float r, g, b, a;
} mel_aces_cg;

mel_p3 mel_color_to_p3(mel_color c);
mel_color mel_color_from_p3(mel_p3 c);

mel_rec2020 mel_color_to_rec2020(mel_color c);
mel_color mel_color_from_rec2020(mel_rec2020 c);

mel_aces_cg mel_color_to_aces_cg(mel_color c);
mel_color mel_color_from_aces_cg(mel_aces_cg c);
