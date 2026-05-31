#pragma once

#include <color/rgba.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct mel_color8 {
    uint8_t r, g, b, a;
} mel_color8;

mel_color8 mel_color8_rgb(uint8_t r, uint8_t g, uint8_t b);
mel_color8 mel_color8_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

mel_color8 mel_color_to_8(mel_color c);
mel_color mel_color_from_8(mel_color8 c);

uint32_t mel_color8_to_u32(mel_color8 c);
mel_color8 mel_color8_from_u32(uint32_t rgba);

bool mel_color8_from_hex(const char *s, mel_color8 *out);
size_t mel_color8_to_hex(mel_color8 c, char *buf, size_t cap, bool with_alpha);

mel_color mel_color_from_u32(uint32_t rgba);
uint32_t mel_color_to_u32(mel_color c);

bool mel_color_from_hex(const char *s, mel_color *out);
size_t mel_color_to_hex(mel_color c, char *buf, size_t cap, bool with_alpha);
