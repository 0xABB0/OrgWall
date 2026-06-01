#include <color/rgba8.h>

#include "color_internal.h"

static uint8_t mel__to_u8(float c) { return (uint8_t)(mel__sat(c) * 255.0f + 0.5f); }

static bool mel__hex_nibble(char ch, uint32_t* out)
{
    if (ch >= '0' && ch <= '9')
    {
        *out = (uint32_t)(ch - '0');
        return true;
    }
    if (ch >= 'a' && ch <= 'f')
    {
        *out = (uint32_t)(ch - 'a' + 10);
        return true;
    }
    if (ch >= 'A' && ch <= 'F')
    {
        *out = (uint32_t)(ch - 'A' + 10);
        return true;
    }
    return false;
}

mel_color8 mel_color8_rgb(uint8_t r, uint8_t g, uint8_t b) { return (mel_color8){ r, g, b, 255 }; }

mel_color8 mel_color8_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) { return (mel_color8){ r, g, b, a }; }

mel_color8 mel_color_to_8(mel_color c)
{
    return (mel_color8){
        mel__to_u8(mel_color_linear_to_srgb(c.r)),
        mel__to_u8(mel_color_linear_to_srgb(c.g)),
        mel__to_u8(mel_color_linear_to_srgb(c.b)),
        mel__to_u8(c.a),
    };
}

mel_color mel_color_from_8(mel_color8 c)
{
    return (mel_color){
        mel_color_srgb_to_linear((float)c.r / 255.0f),
        mel_color_srgb_to_linear((float)c.g / 255.0f),
        mel_color_srgb_to_linear((float)c.b / 255.0f),
        (float)c.a / 255.0f,
    };
}

uint32_t mel_color8_to_u32(mel_color8 c) { return ((uint32_t)c.r << 24) | ((uint32_t)c.g << 16) | ((uint32_t)c.b << 8) | (uint32_t)c.a; }

mel_color8 mel_color8_from_u32(uint32_t rgba)
{
    return (mel_color8){
        (uint8_t)((rgba >> 24) & 0xFFu),
        (uint8_t)((rgba >> 16) & 0xFFu),
        (uint8_t)((rgba >> 8) & 0xFFu),
        (uint8_t)(rgba & 0xFFu),
    };
}

bool mel_color8_from_hex(const char* s, mel_color8* out)
{
    if (!s || !out)
        return false;
    if (*s == '#')
        s++;

    size_t n = 0;
    while (s[n])
        n++;

    uint32_t d[8];
    if (n != 3 && n != 4 && n != 6 && n != 8)
        return false;
    for (size_t i = 0; i < n; i++)
    {
        if (!mel__hex_nibble(s[i], &d[i]))
            return false;
    }

    uint32_t r, g, b, a;
    if (n == 3 || n == 4)
    {
        r = d[0] * 17u;
        g = d[1] * 17u;
        b = d[2] * 17u;
        a = (n == 4) ? d[3] * 17u : 255u;
    }
    else
    {
        r = d[0] * 16u + d[1];
        g = d[2] * 16u + d[3];
        b = d[4] * 16u + d[5];
        a = (n == 8) ? d[6] * 16u + d[7] : 255u;
    }

    *out = (mel_color8){ (uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)a };
    return true;
}

size_t mel_color8_to_hex(mel_color8 c, char* buf, size_t cap, bool with_alpha)
{
    static const char digits[] = "0123456789abcdef";
    size_t            need = with_alpha ? 9 : 7;
    if (!buf || cap < need + 1)
        return need;

    size_t   count = with_alpha ? 8 : 6;
    uint32_t value = with_alpha ? mel_color8_to_u32(c) : (((uint32_t)c.r << 16) | ((uint32_t)c.g << 8) | (uint32_t)c.b);

    buf[0] = '#';
    for (size_t i = 0; i < count; i++)
    {
        uint32_t shift = (count - 1 - i) * 4;
        buf[1 + i] = digits[(value >> shift) & 0xFu];
    }
    buf[need] = '\0';
    return need;
}

mel_color mel_color_from_u32(uint32_t rgba) { return mel_color_from_8(mel_color8_from_u32(rgba)); }

uint32_t mel_color_to_u32(mel_color c) { return mel_color8_to_u32(mel_color_to_8(c)); }

bool mel_color_from_hex(const char* s, mel_color* out)
{
    if (!out)
        return false;
    mel_color8 c8;
    if (!mel_color8_from_hex(s, &c8))
        return false;
    *out = mel_color_from_8(c8);
    return true;
}

size_t mel_color_to_hex(mel_color c, char* buf, size_t cap, bool with_alpha) { return mel_color8_to_hex(mel_color_to_8(c), buf, cap, with_alpha); }
