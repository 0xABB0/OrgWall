#ifndef MEL_FONT_H
#define MEL_FONT_H

#include "vk_context.h"
#include "vk_texture.h"
#include "sprite_batch.h"
#include "math.vec2.h"

#define MEL_FONT_FIRST_CHAR 32
#define MEL_FONT_CHAR_COUNT 96

typedef struct
{
    f32 x0, y0, x1, y1;
    f32 u0, v0, u1, v1;
    f32 xadvance;
} Mel_GlyphInfo;

typedef struct Mel_Font
{
    Mel_VkTexture atlas;
    Mel_GlyphInfo glyphs[MEL_FONT_CHAR_COUNT];
    f32 line_height;
    f32 ascent;
    u32 atlas_width;
    u32 atlas_height;
} Mel_Font;

typedef struct
{
    const char* path;
    const u8* data;
    u32 data_size;
    f32 size;
    u32 atlas_width;
    u32 atlas_height;
} Mel_Font_Opt;

bool mel_font_init_opt(Mel_Font* font, Mel_VkContext* ctx, Mel_Font_Opt opt);
#define mel_font_init(font, ctx, ...) mel_font_init_opt((font), (ctx), (Mel_Font_Opt){__VA_ARGS__})

void mel_font_shutdown(Mel_Font* font, Mel_VkContext* ctx);

void mel_font_draw_text(Mel_Font* font, Mel_SpriteBatch* batch, const char* text, f32 x, f32 y, Mel_Vec4 color);

Mel_Vec2 mel_font_measure_text(Mel_Font* font, const char* text);

#endif
