#pragma once

#include "core.types.h"
#include "event.channel.fwd.h"
#include "string.str8.fwd.h"
#include "font.atlas.fwd.h"
#include "font.desc.fwd.h"
#include "font.descriptor.h"
#include "gpu.texture.h"
#include "collection.slotmap.h"
#include "math.vec2.h"
#include "math.vec4.h"
#include "texture.pool.fwd.h"

struct Mel_Font_Atlas_Entry {
    Mel_Font_Descriptor desc;
    Mel_Gpu_Texture atlas_texture;
    u32 atlas_width, atlas_height;
    Mel_Texture_Handle tex_handle;
    Mel_Font_Desc_Handle font_desc;
};

typedef struct {
    Mel_Font_Desc_Handle desc;
    f32 size;
    u32 atlas_width;
    u32 atlas_height;
} Mel_Font_Atlas_Load_Opt;

Mel_Font_Atlas_Handle mel_font_atlas_load_opt(Mel_Font_Atlas_Load_Opt opt);
#define mel_font_atlas_load(...) mel_font_atlas_load_opt((Mel_Font_Atlas_Load_Opt){__VA_ARGS__})
Mel_Font_Atlas_Entry* mel_font_atlas_get(Mel_Font_Atlas_Handle handle);
Mel_Gpu_Texture*      mel_font_atlas_get_texture(Mel_Font_Atlas_Handle handle);
Mel_Texture_Handle    mel_font_atlas_tex_handle(Mel_Font_Atlas_Handle handle);

void mel_font_atlas_draw_text_ex(Mel_Font_Atlas_Handle handle,
    Mel_Render_List* list, str8 text, f32 x, f32 y, Mel_Vec4 color, u64 sort_key);
#define mel_font_atlas_draw_text(handle, list, text, x, y, color) \
    mel_font_atlas_draw_text_ex((handle), (list), (text), (x), (y), (color), 0)
Mel_Vec2 mel_font_atlas_measure_text(Mel_Font_Atlas_Handle handle, str8 text);

void mel__font_atlas_shutdown(void);
Mel_Font_Atlas_Handle mel__font_atlas_insert_entry(const Mel_Font_Atlas_Entry* entry);

extern Mel_Event_Channel mel_font_pool_ready;
