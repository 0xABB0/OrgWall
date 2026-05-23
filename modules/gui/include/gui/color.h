#pragma once

#include <core/types.h>

typedef struct {
    u8 r, g, b, a;
} Mel_Color;

static inline Mel_Color mel_rgb(u8 r, u8 g, u8 b)
{
    return (Mel_Color){r, g, b, 255};
}

static inline Mel_Color mel_rgba(u8 r, u8 g, u8 b, u8 a)
{
    return (Mel_Color){r, g, b, a};
}
