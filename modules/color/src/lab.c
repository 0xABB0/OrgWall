#include <color/lab.h>

#include <color/space.h>

#include <math.h>

static const double mel__lab_e = 216.0 / 24389.0;
static const double mel__lab_k = 24389.0 / 27.0;

static double mel__lab_f(double t) {
    return t > mel__lab_e ? cbrt(t) : (mel__lab_k * t + 16.0) / 116.0;
}

mel_lab mel_xyz_to_lab(mel_xyz c, mel_xyz white) {
    double fx = mel__lab_f((double)c.x / white.x);
    double fy = mel__lab_f((double)c.y / white.y);
    double fz = mel__lab_f((double)c.z / white.z);
    return (mel_lab){
        (float)(116.0 * fy - 16.0),
        (float)(500.0 * (fx - fy)),
        (float)(200.0 * (fy - fz)),
    };
}

mel_xyz mel_lab_to_xyz(mel_lab c, mel_xyz white) {
    double fy = ((double)c.l + 16.0) / 116.0;
    double fx = fy + (double)c.a / 500.0;
    double fz = fy - (double)c.b / 200.0;
    double fx3 = fx * fx * fx;
    double fz3 = fz * fz * fz;
    double xr = fx3 > mel__lab_e ? fx3 : (116.0 * fx - 16.0) / mel__lab_k;
    double yr = (double)c.l > mel__lab_k * mel__lab_e ? fy * fy * fy : (double)c.l / mel__lab_k;
    double zr = fz3 > mel__lab_e ? fz3 : (116.0 * fz - 16.0) / mel__lab_k;
    return (mel_xyz){
        (float)(xr * white.x),
        (float)(yr * white.y),
        (float)(zr * white.z),
    };
}

mel_lch mel_lab_to_lch(mel_lab c) {
    float chroma = hypotf(c.a, c.b);
    float hue = atan2f(c.b, c.a) * (180.0f / 3.14159265358979323846f);
    if (hue < 0.0f)
        hue += 360.0f;
    return (mel_lch){c.l, chroma, hue};
}

mel_lab mel_lch_to_lab(mel_lch c) {
    float rad = c.h * (3.14159265358979323846f / 180.0f);
    return (mel_lab){c.l, c.c * cosf(rad), c.c * sinf(rad)};
}

mel_lab mel_color_to_lab(mel_color c) {
    return mel_xyz_to_lab(mel_color_to_xyz(c), mel_white_point_xyz(mel_white_d65()));
}

mel_color mel_color_from_lab(mel_lab c, float a) {
    return mel_color_from_xyz(mel_lab_to_xyz(c, mel_white_point_xyz(mel_white_d65())), a);
}

mel_lch mel_color_to_lch(mel_color c) {
    return mel_lab_to_lch(mel_color_to_lab(c));
}

mel_color mel_color_from_lch(mel_lch c, float a) {
    return mel_color_from_lab(mel_lch_to_lab(c), a);
}
