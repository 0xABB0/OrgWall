#include <color/hsv.h>

#include <math.h>

mel_hsv mel_color_to_hsv(mel_color c) {
    float r = mel_color_linear_to_srgb(c.r);
    float g = mel_color_linear_to_srgb(c.g);
    float b = mel_color_linear_to_srgb(c.b);
    float max = fmaxf(r, fmaxf(g, b));
    float min = fminf(r, fminf(g, b));
    float delta = max - min;
    float h = 0.0f;
    float s = max > 0.0f ? delta / max : 0.0f;

    if (delta > 0.0f) {
        if (max == r)
            h = fmodf((g - b) / delta, 6.0f);
        else if (max == g)
            h = (b - r) / delta + 2.0f;
        else
            h = (r - g) / delta + 4.0f;
        h *= 60.0f;
        if (h < 0.0f)
            h += 360.0f;
    }
    return (mel_hsv){h, s, max};
}

mel_color mel_color_from_hsv(mel_hsv hsv, float a) {
    float c = hsv.v * hsv.s;
    float hp = fmodf(hsv.h, 360.0f) / 60.0f;
    if (hp < 0.0f)
        hp += 6.0f;
    float x = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
    float m = hsv.v - c;

    float r = 0.0f, g = 0.0f, b = 0.0f;
    if (hp < 1.0f) {
        r = c;
        g = x;
    } else if (hp < 2.0f) {
        r = x;
        g = c;
    } else if (hp < 3.0f) {
        g = c;
        b = x;
    } else if (hp < 4.0f) {
        g = x;
        b = c;
    } else if (hp < 5.0f) {
        r = x;
        b = c;
    } else {
        r = c;
        b = x;
    }
    return (mel_color){
        mel_color_srgb_to_linear(r + m),
        mel_color_srgb_to_linear(g + m),
        mel_color_srgb_to_linear(b + m),
        a,
    };
}
