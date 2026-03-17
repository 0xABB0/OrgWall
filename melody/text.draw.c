#include "text.draw.h"
#include "render.list.h"
#include "string.str8.h"

static void mel__text_draw_desc(Mel_Font_Descriptor* desc, Mel_Gpu_Texture* texture, u32 mode,
    f32 default_px_range, Mel_Render_List* list, str8 text, Mel_Text_Draw_Opt opt)
{
    assert(desc != nullptr);
    assert(texture != nullptr);
    assert(list != nullptr);

    if (str8_is_empty(text)) return;

    Mel_Text_Style style = opt.style;
    if (style.color.w == 0.0f)
        style.color = mel_vec4(1.0f, 1.0f, 1.0f, 1.0f);
    if (style.outline_color.w == 0.0f)
        style.outline_color = mel_vec4(0.0f, 0.0f, 0.0f, 1.0f);
    if (style.edge == 0.0f)
        style.edge = 0.5f;
    if (style.softness == 0.0f)
        style.softness = 0.08f;
    if (style.px_range == 0.0f)
        style.px_range = default_px_range > 0.0f ? default_px_range : 4.0f;
    f32 draw_scale = opt.scale > 0.0f ? opt.scale : 1.0f;

    f32 cursor_x = opt.x;
    f32 cursor_y = opt.y + desc->ascent * draw_scale;

    for (size i = 0; i < text.len; i++)
    {
        int c = text.data[i];
        if (c == '\n')
        {
            cursor_x = opt.x;
            cursor_y += desc->line_height * draw_scale;
            continue;
        }

        if (c < (int)desc->first_codepoint ||
            c >= (int)(desc->first_codepoint + desc->glyph_count))
            continue;

        Mel_Font_Glyph* g = &desc->glyphs[c - (int)desc->first_codepoint];
        f32 gx = cursor_x + g->x0 * draw_scale;
        f32 gy = cursor_y + g->y0 * draw_scale;
        f32 gw = (g->x1 - g->x0) * draw_scale;
        f32 gh = (g->y1 - g->y0) * draw_scale;

        if (gw > 0 && gh > 0)
        {
            Mel_Text_Entry* e = mel_render_list_push(list, style.sort_key);
            *e = (Mel_Text_Entry){
                .pos = mel_vec2(gx, gy),
                .size = mel_vec2(gw, gh),
                .uv = mel_rect(g->u0, g->v0, g->u1 - g->u0, g->v1 - g->v0),
                .color = style.color,
                .outline_color = style.outline_color,
                .texture = texture,
                .edge = style.edge,
                .softness = style.softness,
                .outline = style.outline,
                .px_range = style.px_range,
                .mode = mode,
            };
        }

        cursor_x += g->xadvance * draw_scale;
    }
}

void mel_text_draw_font_atlas_opt(Mel_Font_Atlas_Handle handle,
    Mel_Render_List* list, str8 text, Mel_Text_Draw_Opt opt)
{
    Mel_Font_Atlas_Entry* entry = mel_font_atlas_get(handle);
    assert(entry != nullptr);
    mel__text_draw_desc(&entry->desc, &entry->atlas_texture, MEL_TEXT_RENDER_ATLAS, 1.0f, list, text, opt);
}

void mel_text_draw_font_sdf_opt(Mel_Font_SDF_Handle handle,
    Mel_Render_List* list, str8 text, Mel_Text_Draw_Opt opt)
{
    Mel_Font_SDF_Entry* entry = mel_font_sdf_get(handle);
    assert(entry != nullptr);
    mel__text_draw_desc(&entry->desc, &entry->texture, MEL_TEXT_RENDER_SDF, entry->px_range, list, text, opt);
}

void mel_text_draw_font_msdf_opt(Mel_Font_MSDF_Handle handle,
    Mel_Render_List* list, str8 text, Mel_Text_Draw_Opt opt)
{
    Mel_Font_MSDF_Entry* entry = mel_font_msdf_get(handle);
    assert(entry != nullptr);
    mel__text_draw_desc(&entry->desc, &entry->texture, MEL_TEXT_RENDER_MSDF, entry->px_range, list, text, opt);
}
