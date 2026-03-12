#pragma once

#include "text.pass.h"
#include "font.atlas.h"
#include "font.sdf.h"
#include "font.msdf.h"
#include "render.list.fwd.h"

typedef struct {
    f32 x;
    f32 y;
    f32 scale;
    Mel_Text_Style style;
} Mel_Text_Draw_Opt;

void mel_text_draw_font_atlas_opt(Mel_Font_Atlas_Pool* pool, Mel_Font_Handle handle,
    Mel_Render_List* list, str8 text, Mel_Text_Draw_Opt opt);
#define mel_text_draw_font_atlas(pool, handle, list, text, ...) \
    mel_text_draw_font_atlas_opt((pool), (handle), (list), (text), (Mel_Text_Draw_Opt){__VA_ARGS__})

void mel_text_draw_font_sdf_opt(Mel_Font_SDF_Pool* pool, Mel_Font_Handle handle,
    Mel_Render_List* list, str8 text, Mel_Text_Draw_Opt opt);
#define mel_text_draw_font_sdf(pool, handle, list, text, ...) \
    mel_text_draw_font_sdf_opt((pool), (handle), (list), (text), (Mel_Text_Draw_Opt){__VA_ARGS__})

void mel_text_draw_font_msdf_opt(Mel_Font_MSDF_Pool* pool, Mel_Font_Handle handle,
    Mel_Render_List* list, str8 text, Mel_Text_Draw_Opt opt);
#define mel_text_draw_font_msdf(pool, handle, list, text, ...) \
    mel_text_draw_font_msdf_opt((pool), (handle), (list), (text), (Mel_Text_Draw_Opt){__VA_ARGS__})
