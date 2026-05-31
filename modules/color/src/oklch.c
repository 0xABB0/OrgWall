#include <color/oklch.h>

#include <math.h>

mel_oklch mel_color_to_oklch(mel_color c) {
    mel_oklab o = mel_color_to_oklab(c);
    float chroma = hypotf(o.a, o.b);
    float hue = atan2f(o.b, o.a) * (180.0f / 3.14159265358979323846f);
    if (hue < 0.0f)
        hue += 360.0f;
    return (mel_oklch){o.l, chroma, hue};
}

mel_color mel_color_from_oklch(mel_oklch o, float a) {
    float rad = o.h * (3.14159265358979323846f / 180.0f);
    mel_oklab lab = {o.l, o.c * cosf(rad), o.c * sinf(rad)};
    return mel_color_from_oklab(lab, a);
}
