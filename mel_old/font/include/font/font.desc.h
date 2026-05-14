#pragma once

#include <core/types.h>
#include "font.desc.fwd.h"
#include <string/string.str8.fwd.h>

#include <stb_truetype.h>

struct Mel_Font_Desc {
    u8*             data;
    i64             data_size;
    stbtt_fontinfo  info;
    i32             ascent;
    i32             descent;
    i32             line_gap;
    i32             units_per_em;
};

Mel_Font_Desc_Handle mel_font_desc_load_ttf(str8 path);
Mel_Font_Desc*       mel_font_desc_get(Mel_Font_Desc_Handle handle);
