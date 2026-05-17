#pragma once

#include <core/types.h>
#include <string/str8.fwd.h>
#include "font.msdf.fwd.h"
#include <font/font.desc.fwd.h>
#include <font/font.descriptor.h>
#include "gpu.texture.h"
#include <math.vector/vec2.h>

struct Mel_Font_MSDF_Entry {
    Mel_Font_Descriptor desc;
    Mel_Gpu_Texture texture;
    u32 atlas_width, atlas_height;
    f32 px_range;
    Mel_Font_Desc_Handle font_desc;
};

typedef struct {
    Mel_Font_Desc_Handle desc;
    f32 size;
    u32 atlas_width;
    u32 atlas_height;
    u32 padding;
    f32 px_range;
} Mel_Font_MSDF_Load_Opt;

Mel_Font_MSDF_Handle  mel_font_msdf_load_opt(Mel_Font_MSDF_Load_Opt opt);
#define mel_font_msdf_load(...) mel_font_msdf_load_opt((Mel_Font_MSDF_Load_Opt){__VA_ARGS__})
Mel_Font_MSDF_Entry*  mel_font_msdf_get(Mel_Font_MSDF_Handle handle);
Mel_Gpu_Texture*      mel_font_msdf_get_texture(Mel_Font_MSDF_Handle handle);
Mel_Vec2              mel_font_msdf_measure_text(Mel_Font_MSDF_Handle handle, str8 text);
