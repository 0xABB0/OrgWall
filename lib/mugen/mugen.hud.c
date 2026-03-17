#include "mugen.hud.h"
#include "font.atlas.h"
#include "sprite.pass.h"
#include "render.list.h"
#include "math.scalar.h"
#include "math.vec2.h"
#include "math.vec4.h"
#include "math.geo.rect.h"
#include "string.str8.h"
#include <stdio.h>
#include <string.h>

#define HUD_LAYER 4
#define MID_LERP_SPEED 2.0f

static u64 hud_key(void)
{
    return mel_sort_key_sprite(HUD_LAYER, 0, 0, 0);
}

static void draw_sff_sprite(Mugen_Hud* hud, Mel_Render_List* list,
    Mugen_Spr_Ref ref, f32 x, f32 y, i8 facing)
{
    if (ref.group == 0 && ref.number == 0) return;

    u32 frame_idx = mugen_sff_find_frame(hud->fight_sff, ref.group, ref.number);
    Mugen_Sff_Entry* entry = &hud->fight_sff->entries[frame_idx];
    Mel_Rect uv = mel_sprite_sheet_frame(&hud->fight_sff->sheet, frame_idx);

    f32 w = (f32)entry->width * hud->scale_x;
    f32 h = (f32)entry->height * hud->scale_y;

    f32 draw_x = x - (f32)entry->offset_x * hud->scale_x;
    f32 draw_y = y - (f32)entry->offset_y * hud->scale_y;

    if (facing == -1)
    {
        uv.x = uv.x + uv.w;
        uv.w = -uv.w;
        draw_x = x - (w - (f32)entry->offset_x * hud->scale_x);
    }

    mel_draw_sprite(list,
        .pos = mel_vec2(draw_x, draw_y),
        .size = mel_vec2(w, h),
        .color = mel_vec4(1, 1, 1, 1),
        .tex = hud->fight_tex,
        .uv = uv,
        .layer = HUD_LAYER);
}

static void draw_sff_sprite_clipped(Mugen_Hud* hud, Mel_Render_List* list,
    Mugen_Spr_Ref ref, f32 x, f32 y, i8 facing,
    i32 range_start, i32 range_end, f32 ratio)
{
    if (ref.group == 0 && ref.number == 0) return;
    if (ratio <= 0.0f) return;

    u32 frame_idx = mugen_sff_find_frame(hud->fight_sff, ref.group, ref.number);
    Mugen_Sff_Entry* entry = &hud->fight_sff->entries[frame_idx];
    Mel_Rect uv = mel_sprite_sheet_frame(&hud->fight_sff->sheet, frame_idx);

    f32 sx = hud->scale_x;
    f32 sy = hud->scale_y;
    f32 off_x = (f32)entry->offset_x;
    f32 sprite_w = (f32)entry->width;
    f32 h = (f32)entry->height * sy;
    f32 draw_y = y - (f32)entry->offset_y * sy;

    f32 img_start, img_end;
    if (facing == -1)
    {
        img_start = off_x - (f32)range_start;
        img_end = off_x - (f32)range_end;
    }
    else
    {
        img_start = (f32)range_start + off_x;
        img_end = (f32)range_end + off_x;
    }

    f32 vis_start = img_start;
    f32 vis_end = img_start + (img_end - img_start) * ratio;

    if (vis_start > vis_end)
    {
        f32 tmp = vis_start;
        vis_start = vis_end;
        vis_end = tmp;
    }

    if (vis_start < 0) vis_start = 0;
    if (vis_end > sprite_w) vis_end = sprite_w;
    if (vis_start >= vis_end) return;

    f32 clip_w_px = vis_end - vis_start;
    f32 uv_per_px = uv.w / sprite_w;
    f32 new_uv_x = uv.x + vis_start * uv_per_px;
    f32 new_uv_w = clip_w_px * uv_per_px;

    f32 draw_x;
    if (facing == -1)
    {
        draw_x = x + (off_x - vis_end) * sx;
        new_uv_x = new_uv_x + new_uv_w;
        new_uv_w = -new_uv_w;
    }
    else
    {
        draw_x = (x - off_x * sx) + vis_start * sx;
    }

    f32 draw_w = clip_w_px * sx;

    mel_draw_sprite(list,
        .pos = mel_vec2(draw_x, draw_y),
        .size = mel_vec2(draw_w, h),
        .color = mel_vec4(1, 1, 1, 1),
        .tex = hud->fight_tex,
        .uv = mel_rect(new_uv_x, uv.y, new_uv_w, uv.h),
        .layer = HUD_LAYER);
}

static void draw_bar(Mugen_Hud* hud, Mel_Render_List* list,
    Mugen_Fightdef_Bar_Player* bar, f32 life_ratio, f32* mid_ratio)
{
    f32 px = (f32)bar->pos_x * hud->scale_x;
    f32 py = (f32)bar->pos_y * hud->scale_y;

    draw_sff_sprite(hud, list, bar->bg1_spr, px, py, bar->bg1_facing);

    if (*mid_ratio > life_ratio)
        *mid_ratio = mel_lerpf(*mid_ratio, life_ratio, MID_LERP_SPEED * (1.0f / 60.0f));
    else
        *mid_ratio = life_ratio;

    f32 mx = px + (f32)bar->mid_offset_x * hud->scale_x;
    f32 my = py + (f32)bar->mid_offset_y * hud->scale_y;
    draw_sff_sprite_clipped(hud, list, bar->mid_spr, mx, my, bar->mid_facing,
        bar->range_x_start, bar->range_x_end, *mid_ratio);

    draw_sff_sprite_clipped(hud, list, bar->front_spr, px, py, bar->front_facing,
        bar->range_x_start, bar->range_x_end, life_ratio);
}

static void draw_lifebar(Mugen_Hud* hud, Mugen_Hud_State* state, Mel_Render_List* list)
{
    draw_bar(hud, list, &hud->fightdef->lifebar.p1, state->p1_life_ratio, &hud->p1_mid_ratio);
    draw_bar(hud, list, &hud->fightdef->lifebar.p2, state->p2_life_ratio, &hud->p2_mid_ratio);
}

static void draw_powerbar(Mugen_Hud* hud, Mugen_Hud_State* state, Mel_Render_List* list)
{
    draw_bar(hud, list, &hud->fightdef->powerbar.p1, state->p1_power_ratio, &hud->p1_power_mid);
    draw_bar(hud, list, &hud->fightdef->powerbar.p2, state->p2_power_ratio, &hud->p2_power_mid);
}

static void draw_hud_text(Mugen_Hud* hud, Mel_Render_List* list,
    str8 text, f32 x, f32 y, Mugen_Font_Ref fref)
{
    if (fref.fontno == 0) return;
    if (text.len == 0) return;

    Mel_Vec2 sz = mel_font_atlas_measure_text(hud->font, text);

    f32 draw_x = x;
    if (fref.alignment == 0)
        draw_x = x - sz.x * 0.5f;
    else if (fref.alignment == -1)
        draw_x = x - sz.x;

    mel_font_atlas_draw_text_ex(hud->font, list, text,
        draw_x, y, mel_vec4(1, 1, 1, 1), hud_key());
}

static void draw_face(Mugen_Hud* hud, Mel_Render_List* list)
{
    Mugen_Fightdef_Face* face = &hud->fightdef->face;

    f32 p1x = (f32)face->p1.pos_x * hud->scale_x;
    f32 p1y = (f32)face->p1.pos_y * hud->scale_y;
    draw_sff_sprite(hud, list, face->p1.bg_spr, p1x, p1y, face->p1.bg_facing);

    f32 p2x = (f32)face->p2.pos_x * hud->scale_x;
    f32 p2y = (f32)face->p2.pos_y * hud->scale_y;
    draw_sff_sprite(hud, list, face->p2.bg_spr, p2x, p2y, face->p2.bg_facing);
}

static void draw_name(Mugen_Hud* hud, Mel_Render_List* list)
{
    (void)hud;
    (void)list;
}

static void draw_time(Mugen_Hud* hud, Mugen_Hud_State* state, Mel_Render_List* list)
{
    Mugen_Fightdef_Time* t = &hud->fightdef->time;
    f32 tx = (f32)(t->pos_x + t->counter_offset_x) * hud->scale_x;
    f32 ty = (f32)(t->pos_y + t->counter_offset_y) * hud->scale_y;

    char buf[16];
    snprintf(buf, sizeof(buf), "%d", state->time_count);
    str8 text = { .data = (u8*)buf, .len = (size)strlen(buf) };
    draw_hud_text(hud, list, text, tx, ty, t->counter_font);
}

void mugen_hud_draw(Mugen_Hud* hud, Mugen_Hud_State* state, Mel_Render_List* list)
{
    if (!hud->fightdef) return;

    draw_lifebar(hud, state, list);
    draw_powerbar(hud, state, list);
    draw_face(hud, list);
    draw_name(hud, list);
    draw_time(hud, state, list);
}
