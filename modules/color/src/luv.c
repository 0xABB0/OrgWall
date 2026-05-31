#include <color/luv.h>

#include <color/space.h>

#include <math.h>

static const double mel__luv_e = 216.0 / 24389.0;
static const double mel__luv_k = 24389.0 / 27.0;

mel_luv mel_xyz_to_luv(mel_xyz c, mel_xyz white) {
    double yr = (double)c.y / white.y;
    double l = yr > mel__luv_e ? 116.0 * cbrt(yr) - 16.0 : mel__luv_k * yr;

    double d = (double)c.x + 15.0 * c.y + 3.0 * c.z;
    double dn = (double)white.x + 15.0 * white.y + 3.0 * white.z;
    double up = d != 0.0 ? 4.0 * c.x / d : 0.0;
    double vp = d != 0.0 ? 9.0 * c.y / d : 0.0;
    double upn = 4.0 * white.x / dn;
    double vpn = 9.0 * white.y / dn;

    return (mel_luv){
        (float)l,
        (float)(13.0 * l * (up - upn)),
        (float)(13.0 * l * (vp - vpn)),
    };
}

mel_xyz mel_luv_to_xyz(mel_luv c, mel_xyz white) {
    if (c.l <= 0.0f)
        return (mel_xyz){0.0f, 0.0f, 0.0f};

    double dn = (double)white.x + 15.0 * white.y + 3.0 * white.z;
    double upn = 4.0 * white.x / dn;
    double vpn = 9.0 * white.y / dn;

    double l = (double)c.l;
    double y = l > mel__luv_k * mel__luv_e ? pow((l + 16.0) / 116.0, 3.0) : l / mel__luv_k;
    y *= white.y;

    double a = (1.0 / 3.0) * (52.0 * l / ((double)c.u + 13.0 * l * upn) - 1.0);
    double b = -5.0 * y;
    double cc = -1.0 / 3.0;
    double d = y * (39.0 * l / ((double)c.v + 13.0 * l * vpn) - 5.0);
    double x = (d - b) / (a - cc);
    double z = x * a + b;

    return (mel_xyz){(float)x, (float)y, (float)z};
}

mel_lchuv mel_luv_to_lchuv(mel_luv c) {
    float chroma = hypotf(c.u, c.v);
    float hue = atan2f(c.v, c.u) * (180.0f / 3.14159265358979323846f);
    if (hue < 0.0f)
        hue += 360.0f;
    return (mel_lchuv){c.l, chroma, hue};
}

mel_luv mel_lchuv_to_luv(mel_lchuv c) {
    float rad = c.h * (3.14159265358979323846f / 180.0f);
    return (mel_luv){c.l, c.c * cosf(rad), c.c * sinf(rad)};
}

mel_luv mel_color_to_luv(mel_color c) {
    return mel_xyz_to_luv(mel_color_to_xyz(c), mel_white_point_xyz(mel_white_d65()));
}

mel_color mel_color_from_luv(mel_luv c, float a) {
    return mel_color_from_xyz(mel_luv_to_xyz(c, mel_white_point_xyz(mel_white_d65())), a);
}
