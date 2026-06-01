#include <color/hwb.h>

#include <color/hsv.h>

#include <math.h>

mel_hwb mel_color_to_hwb(mel_color c)
{
    float   r = mel_color_linear_to_srgb(c.r);
    float   g = mel_color_linear_to_srgb(c.g);
    float   b = mel_color_linear_to_srgb(c.b);
    mel_hsv hsv = mel_color_to_hsv(c);
    float   w = fminf(r, fminf(g, b));
    float   bl = 1.0f - fmaxf(r, fmaxf(g, b));
    return (mel_hwb){ hsv.h, w, bl };
}

mel_color mel_color_from_hwb(mel_hwb c, float a)
{
    float w = c.w;
    float bl = c.b;
    if (w + bl >= 1.0f)
    {
        float gray = w / (w + bl);
        float lin = mel_color_srgb_to_linear(gray);
        return (mel_color){ lin, lin, lin, a };
    }

    mel_color base = mel_color_from_hsv((mel_hsv){ c.h, 1.0f, 1.0f }, a);
    float     br = mel_color_linear_to_srgb(base.r);
    float     bg = mel_color_linear_to_srgb(base.g);
    float     bb = mel_color_linear_to_srgb(base.b);
    float     f = 1.0f - w - bl;
    return (mel_color){
        mel_color_srgb_to_linear(br * f + w),
        mel_color_srgb_to_linear(bg * f + w),
        mel_color_srgb_to_linear(bb * f + w),
        a,
    };
}
