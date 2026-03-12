#include "game_draw.h"
#include "mugen.fighter.h"
#include "stage.h"
#include "mugen.cns.h"
#include "mugen.air.h"
#include "mugen.sff.h"
#include "render.list.h"
#include "sprite.pass.h"
#include "sprite.sheet.h"
#include "math.scalar.h"
#include "math.vec2.h"
#include "math.vec4.h"
#include "math.geo.rect.h"

static f32 world_to_screen_x(f32 wx, f32 cam_x, f32 scale_x)
{
    return (wx - cam_x) * scale_x + (f32)GAME_W * 0.5f;
}

static f32 world_to_screen_y(f32 wy, f32 zoffset, f32 scale_y)
{
    return zoffset * scale_y - wy * scale_y;
}

static void draw_box_outline(Mel_Render_List* list, f32 bx, f32 by, f32 bw, f32 bh,
                              Mel_Vec4 color, f32 thickness)
{
    mel_draw_sprite(list, .pos = mel_vec2(bx, by),
        .size = mel_vec2(bw, thickness), .color = color, .layer = 3);
    mel_draw_sprite(list, .pos = mel_vec2(bx, by + bh - thickness),
        .size = mel_vec2(bw, thickness), .color = color, .layer = 3);
    mel_draw_sprite(list, .pos = mel_vec2(bx, by),
        .size = mel_vec2(thickness, bh), .color = color, .layer = 3);
    mel_draw_sprite(list, .pos = mel_vec2(bx + bw - thickness, by),
        .size = mel_vec2(thickness, bh), .color = color, .layer = 3);
}

static void draw_mugen_sprite_at(f32 world_x, f32 world_y, bool facing_right,
                                  Mugen_Sff* sff, Mel_Texture_Handle tex,
                                  u16 group, u16 number, bool frame_flip_h,
                                  Mugen_Camera* cam, f32 zoffset, f32 scale_x, f32 scale_y,
                                  u8 layer, Mel_Render_List* list)
{
    u32 frame_idx = mugen_sff_find_frame(sff, group, number);
    Mugen_Sff_Entry* entry = &sff->entries[frame_idx];
    Mel_Rect uv = mel_sprite_sheet_frame(&sff->sheet, frame_idx);

    f32 w = (f32)entry->width * scale_x;
    f32 h = (f32)entry->height * scale_y;

    f32 screen_x = world_to_screen_x(world_x, cam->x, scale_x);
    f32 screen_y = world_to_screen_y(world_y, zoffset, scale_y);

    f32 draw_x = screen_x - (f32)entry->offset_x * scale_x;
    f32 draw_y = screen_y - (f32)entry->offset_y * scale_y;

    bool flip = facing_right ^ frame_flip_h;
    if (!flip)
    {
        uv.x = uv.x + uv.w;
        uv.w = -uv.w;
        draw_x = screen_x - (w - (f32)entry->offset_x * scale_x);
    }

    mel_draw_sprite(list,
        .pos = mel_vec2(draw_x, draw_y),
        .size = mel_vec2(w, h),
        .color = mel_vec4(1, 1, 1, 1),
        .tex = tex,
        .uv = uv,
        .layer = layer);
}

void game_draw_afterimage(Mugen_Char_State* st, Mugen_Char* mc, Mel_Texture_Handle tex,
    Mugen_Camera* cam, f32 zoffset, f32 scale_x, f32 scale_y, Mel_Render_List* list)
{
    if (!mc->loaded) return;
    u32 count = mugen_afterimage_visible_count(st);
    if (count == 0) return;

    for (u32 i = 0; i < count; i++)
    {
        Mugen_AfterImage_Snap* snap = mugen_afterimage_get(st, i);
        if (!snap) continue;

        Mugen_Air_Action* action = mugen_air_find_action(&mc->air, snap->anim);
        if (!action || snap->anim_frame_index >= action->frame_count) continue;

        Mugen_Air_Frame* frame = &action->frames[snap->anim_frame_index];
        bool facing_right = snap->facing > 0.0f;

        f32 alpha = 1.0f - (f32)(i + 1) / (f32)(count + 1);

        u32 frame_idx = mugen_sff_find_frame(&mc->sff, frame->group, frame->number);
        Mugen_Sff_Entry* entry = &mc->sff.entries[frame_idx];
        Mel_Rect uv = mel_sprite_sheet_frame(&mc->sff.sheet, frame_idx);

        f32 w = (f32)entry->width * scale_x;
        f32 h = (f32)entry->height * scale_y;

        f32 screen_x = world_to_screen_x(snap->pos_x, cam->x, scale_x);
        f32 screen_y = world_to_screen_y(snap->pos_y, zoffset, scale_y);

        f32 draw_x = screen_x - (f32)entry->offset_x * scale_x;
        f32 draw_y = screen_y - (f32)entry->offset_y * scale_y;

        bool flip = facing_right ^ frame->flip_h;
        if (!flip)
        {
            uv.x = uv.x + uv.w;
            uv.w = -uv.w;
            draw_x = screen_x - (w - (f32)entry->offset_x * scale_x);
        }

        f32 r = 1.0f, g = 1.0f, b = 1.0f;
        for (i32 c = 0; c < 3; c++)
        {
            f32 bright = (f32)st->afterimage.palbright[c] / 256.0f;
            f32 contrast = (f32)st->afterimage.palcontrast[c] / 256.0f;
            f32 val = bright + contrast;
            if (val > 1.0f) val = 1.0f;
            if (val < 0.0f) val = 0.0f;
            if (c == 0) r = val;
            else if (c == 1) g = val;
            else b = val;
        }

        mel_draw_sprite(list,
            .pos = mel_vec2(draw_x, draw_y),
            .size = mel_vec2(w, h),
            .color = mel_vec4(r, g, b, alpha * 0.5f),
            .tex = tex,
            .uv = uv,
            .layer = 0);
    }
}

void game_draw_fighter(Fighter* f, Mugen_Char* mc, Mel_Texture_Handle tex,
    Mugen_Camera* cam, f32 zoffset, f32 scale_x, f32 scale_y, Mel_Render_List* list)
{
    if (!mc->loaded) return;

    Mugen_Air_Frame* frame = mugen_state_anim_frame(&f->cns_state);
    if (!frame) return;

    draw_mugen_sprite_at(f->x, f->y, f->facing_right,
                         &mc->sff, tex, frame->group, frame->number,
                         frame->flip_h, cam, zoffset, scale_x, scale_y, 1, list);
}

void game_draw_helper(Fighter_Helper* h, Mugen_Char* mc, Mel_Texture_Handle tex,
    Mugen_Camera* cam, f32 zoffset, f32 scale_x, f32 scale_y, Mel_Render_List* list)
{
    if (!mc->loaded) return;

    Mugen_Air_Frame* frame = mugen_state_anim_frame(&h->cns_state);
    if (!frame) return;

    draw_mugen_sprite_at(h->x, h->y, h->facing_right,
                         &mc->sff, tex, frame->group, frame->number,
                         frame->flip_h, cam, zoffset, scale_x, scale_y, 1, list);
}

static void draw_debug_box(Fighter_Box box, Mugen_Camera* cam, f32 zoffset,
                            f32 scale_x, f32 scale_y, Mel_Vec4 color,
                            Mel_Render_List* list)
{
    f32 sx = world_to_screen_x(box.x, cam->x, scale_x);
    f32 sy = world_to_screen_y(box.y + box.h, zoffset, scale_y);
    f32 sw = box.w * scale_x;
    f32 sh = box.h * scale_y;
    draw_box_outline(list, sx, sy, sw, sh, color, 1.0f);
}

void game_draw_debug_boxes(Fighter* f, Mugen_Camera* cam, f32 zoffset,
    f32 scale_x, f32 scale_y, Mel_Render_List* list)
{
    Fighter_Box hurt = fighter_hurtbox(f);
    draw_debug_box(hurt, cam, zoffset, scale_x, scale_y,
        mel_vec4(0.0f, 1.0f, 0.0f, 1.0f), list);

    if (fighter_has_active_hitbox(f))
    {
        Fighter_Box hit = fighter_hitbox(f);
        draw_debug_box(hit, cam, zoffset, scale_x, scale_y,
            mel_vec4(1.0f, 0.0f, 0.0f, 1.0f), list);
    }
}

void game_draw_helper_debug_boxes(Fighter_Helper* h, Mugen_Camera* cam, f32 zoffset,
    f32 scale_x, f32 scale_y, Mel_Render_List* list)
{
    Fighter_Box hurt = helper_hurtbox(h);
    draw_debug_box(hurt, cam, zoffset, scale_x, scale_y,
        mel_vec4(0.0f, 0.8f, 0.8f, 1.0f), list);

    if (helper_has_active_hitbox(h))
    {
        Fighter_Box hit = helper_hitbox(h);
        draw_debug_box(hit, cam, zoffset, scale_x, scale_y,
            mel_vec4(1.0f, 0.5f, 0.0f, 1.0f), list);
    }
}

void game_draw_stage_layer(Mugen_Stage* stage, Mugen_Sff* sff,
    Mel_Texture_Handle tex, Mugen_Camera* cam,
    u8 target_layer, Mel_Render_List* list)
{
    if (!sff || !stage->bgs) return;

    u8 sprite_layer = (target_layer == 0) ? 0 : 2;

    f32 sx = (f32)GAME_W / (f32)stage->localcoord_w;
    f32 sy = (f32)GAME_H / (f32)stage->localcoord_h;

    for (u32 i = 0; i < stage->bg_count; i++)
    {
        Mugen_Stage_BG* bg = &stage->bgs[i];
        if (bg->layerno != target_layer) continue;

        u32 frame_idx = mugen_sff_find_frame(sff, bg->sprite_group, bg->sprite_number);
        Mugen_Sff_Entry* entry = &sff->entries[frame_idx];
        Mel_Rect uv = mel_sprite_sheet_frame(&sff->sheet, frame_idx);

        f32 w = (f32)entry->width * sx;
        f32 h = (f32)entry->height * sy;

        if (bg->type == MUGEN_BG_PARALLAX)
        {
            f32 avg_xscale = (bg->xscale_top + bg->xscale_bot) * 0.5f;
            w *= avg_xscale;
        }

        f32 base_x = (bg->start_x - cam->x * bg->delta_x) * sx + (f32)GAME_W * 0.5f;
        f32 base_y = (bg->start_y - cam->y * bg->delta_y) * sy;

        base_x -= (f32)entry->offset_x * sx;
        base_y -= (f32)entry->offset_y * sy;

        f32 alpha = 1.0f;
        if (bg->trans == MUGEN_TRANS_ADDALPHA)
            alpha = (f32)bg->alpha_src / 256.0f;
        else if (bg->trans == MUGEN_TRANS_ADD1)
            alpha = 0.5f;
        else if (bg->trans == MUGEN_TRANS_SUB)
            alpha = 0.25f;

        Mel_Vec4 color = mel_vec4(1, 1, 1, alpha);

        f32 tile_w = w + (f32)bg->tilespacing_x * sx;
        f32 tile_h = h + (f32)bg->tilespacing_y * sy;
        if (tile_w < 1.0f) tile_w = w;
        if (tile_h < 1.0f) tile_h = h;

        i32 x_start = 0, x_end = 0;
        if (bg->tile_x == 0)
        {
            x_start = 0;
            x_end = 0;
        }
        else if (bg->tile_x == 1)
        {
            x_start = (i32)mel_floorf((0.0f - base_x) / tile_w) - 1;
            x_end = (i32)mel_ceilf(((f32)GAME_W - base_x) / tile_w) + 1;
        }
        else
        {
            x_start = 0;
            x_end = bg->tile_x - 1;
        }

        i32 y_start = 0, y_end = 0;
        if (bg->tile_y == 0)
        {
            y_start = 0;
            y_end = 0;
        }
        else if (bg->tile_y == 1)
        {
            y_start = (i32)mel_floorf((0.0f - base_y) / tile_h) - 1;
            y_end = (i32)mel_ceilf(((f32)GAME_H - base_y) / tile_h) + 1;
        }
        else
        {
            y_start = 0;
            y_end = bg->tile_y - 1;
        }

        for (i32 ty = y_start; ty <= y_end; ty++)
        {
            for (i32 tx = x_start; tx <= x_end; tx++)
            {
                f32 dx = base_x + (f32)tx * tile_w;
                f32 dy = base_y + (f32)ty * tile_h;

                if (dx + w < 0 || dx > (f32)GAME_W) continue;
                if (dy + h < 0 || dy > (f32)GAME_H) continue;

                mel_draw_sprite(list,
                    .pos = mel_vec2(dx, dy),
                    .size = mel_vec2(w, h),
                    .color = color,
                    .tex = tex,
                    .uv = uv,
                    .layer = sprite_layer);
            }
        }
    }
}

