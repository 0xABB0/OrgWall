#pragma once

#include "core.types.h"
#include "font.descriptor.fwd.h"

struct Mel_Font_Glyph {
    f32 x0, y0, x1, y1;
    f32 u0, v0, u1, v1;
    f32 xadvance;
};

struct Mel_Font_Descriptor {
    Mel_Font_Glyph* glyphs;
    u32 glyph_count;
    u32 first_codepoint;
    f32 line_height;
    f32 ascent;
};
