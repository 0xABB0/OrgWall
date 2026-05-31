#include <color/hsl.h>

#include <math.h>

mel_hsl mel_color_to_hsl(mel_color c) {
    float r = mel_color_linear_to_srgb(c.r);
    float g = mel_color_linear_to_srgb(c.g);
    float b = mel_color_linear_to_srgb(c.b);
    float max = fmaxf(r, fmaxf(g, b));
    float min = fminf(r, fminf(g, b));
    float delta = max - min;
    float l = (max + min) * 0.5f;
    float h = 0.0f;
    float s = 0.0f;

    if (delta > 0.0f) {
        s = delta / (1.0f - fabsf(2.0f * l - 1.0f));
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
    return (mel_hsl){h, s, l};
}

mel_color mel_color_from_hsl(mel_hsl hsl, float a) {
    float c = (1.0f - fabsf(2.0f * hsl.l - 1.0f)) * hsl.s;
    float hp = fmodf(hsl.h, 360.0f) / 60.0f;
    if (hp < 0.0f)
        hp += 6.0f;
    float x = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
    float m = hsl.l - c * 0.5f;

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
