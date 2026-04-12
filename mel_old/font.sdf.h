#pragma once

#include "core.types.h"
#include "string.str8.fwd.h"
#include "font.sdf.fwd.h"
#include "font.desc.fwd.h"
#include "font.descriptor.h"
#include "gpu.texture.h"
#include "math.vec2.h"

struct Mel_Font_SDF_Entry {
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
} Mel_Font_SDF_Load_Opt;

Mel_Font_SDF_Handle  mel_font_sdf_load_opt(Mel_Font_SDF_Load_Opt opt);
#define mel_font_sdf_load(...) mel_font_sdf_load_opt((Mel_Font_SDF_Load_Opt){__VA_ARGS__})
Mel_Font_SDF_Entry*  mel_font_sdf_get(Mel_Font_SDF_Handle handle);
Mel_Gpu_Texture*     mel_font_sdf_get_texture(Mel_Font_SDF_Handle handle);
Mel_Vec2             mel_font_sdf_measure_text(Mel_Font_SDF_Handle handle, str8 text);
