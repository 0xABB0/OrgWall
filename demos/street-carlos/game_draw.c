#include "game_draw.h"
#include "stage.h"
#include "mugen.air.h"
#include "mugen.sff.h"
#include "anim.player.h"
#include "render.list.h"
#include "sprite.pass.h"
#include "sprite.sheet.h"
#include "math.vec2.h"
#include "math.vec4.h"
#include "math.geo.rect.h"
#include "hash.xxh.h"

static u64 s_frame_prop;
static bool s_prop_init = false;

static void ensure_prop(void)
{
    if (s_prop_init) return;
    s_frame_prop = mel_xxh3_64("frame", 5);
    s_prop_init = true;
}

static f32 game_to_screen_y(f32 game_y, f32 h)
{
    return STAGE_FLOOR_Y - game_y - h;
}

static void draw_box_outline(Mel_Render_List* list, Fighter_Box box, Mel_Vec4 color, f32 thickness)
{
    f32 sy = game_to_screen_y(box.y, box.h);

    mel_draw_sprite(list, .pos = mel_vec2(box.x, sy),
        .size = mel_vec2(box.w, thickness), .color = color);
    mel_draw_sprite(list, .pos = mel_vec2(box.x, sy + box.h - thickness),
        .size = mel_vec2(box.w, thickness), .color = color);
    mel_draw_sprite(list, .pos = mel_vec2(box.x, sy),
        .size = mel_vec2(thickness, box.h), .color = color);
    mel_draw_sprite(list, .pos = mel_vec2(box.x + box.w - thickness, sy),
        .size = mel_vec2(thickness, box.h), .color = color);
}

static void draw_mugen_sprite_at(f32 feet_x, f32 feet_y, bool facing_right,
                                  Mugen_Sff* sff, Mel_Texture_Handle tex,
                                  u16 group, u16 number, bool frame_flip_h,
                                  Mel_Render_List* list)
{
    u32 frame_idx = mugen_sff_find_frame(sff, group, number);
    Mugen_Sff_Entry* entry = &sff->entries[frame_idx];
    Mel_Rect uv = mel_sprite_sheet_frame(&sff->sheet, frame_idx);

    f32 w = (f32)entry->width;
    f32 h = (f32)entry->height;

    f32 screen_y = STAGE_FLOOR_Y - feet_y;

    f32 draw_x = feet_x - (f32)entry->offset_x;
    f32 draw_y = screen_y - (f32)entry->offset_y;

    bool flip = facing_right ^ frame_flip_h;
    if (!flip)
    {
        uv.x = uv.x + uv.w;
        uv.w = -uv.w;
        draw_x = feet_x - (w - (f32)entry->offset_x);
    }

    mel_draw_sprite(list,
        .pos = mel_vec2(draw_x, draw_y),
        .size = mel_vec2(w, h),
        .color = mel_vec4(1, 1, 1, 1),
        .tex = tex,
        .uv = uv);
}

void game_draw_fighter(Fighter* f, Mugen_Char* mc, Mel_Render_List* list)
{
    ensure_prop();

    if (!mc->loaded) return;

    Mugen_Air_Action* action = mugen_air_find_action(&mc->air, f->current_action);
    if (!action) return;

    f32 frame_f;
    mel_anim_player_get_float(&f->player, s_frame_prop, f->player.alloc, &frame_f);
    u32 frame_idx = (u32)frame_f;
    if (frame_idx >= action->frame_count)
        frame_idx = action->frame_count - 1;

    Mugen_Air_Frame* frame = &action->frames[frame_idx];

    draw_mugen_sprite_at(f->x, f->y, f->facing_right,
                         &mc->sff, mc->tex_handle, frame->group, frame->number,
                         frame->flip_h, list);
}

void game_draw_helper(Fighter_Helper* h, Mugen_Char* mc, Mel_Render_List* list)
{
    ensure_prop();

    if (!mc->loaded) return;

    Mugen_Air_Action* action = mugen_air_find_action(&mc->air, h->current_action);
    if (!action) return;

    f32 frame_f;
    mel_anim_player_get_float(&h->player, s_frame_prop, h->player.alloc, &frame_f);
    u32 frame_idx = (u32)frame_f;
    if (frame_idx >= action->frame_count)
        frame_idx = action->frame_count - 1;

    Mugen_Air_Frame* frame = &action->frames[frame_idx];

    draw_mugen_sprite_at(h->x, h->y, h->facing_right,
                         &mc->sff, mc->tex_handle, frame->group, frame->number,
                         frame->flip_h, list);
}

void game_draw_debug_boxes(Fighter* f, Mel_Render_List* list)
{
    Fighter_Box hurt = fighter_hurtbox(f);
    draw_box_outline(list, hurt, mel_vec4(0.0f, 1.0f, 0.0f, 1.0f), 1.0f);

    if (fighter_has_active_hitbox(f))
    {
        Fighter_Box hit = fighter_hitbox(f);
        draw_box_outline(list, hit, mel_vec4(1.0f, 0.0f, 0.0f, 1.0f), 1.0f);
    }
}

void game_draw_helper_debug_boxes(Fighter_Helper* h, Mel_Render_List* list)
{
    Fighter_Box hurt = helper_hurtbox(h);
    draw_box_outline(list, hurt, mel_vec4(0.0f, 0.8f, 0.8f, 1.0f), 1.0f);

    if (helper_has_active_hitbox(h))
    {
        Fighter_Box hit = helper_hitbox(h);
        draw_box_outline(list, hit, mel_vec4(1.0f, 0.5f, 0.0f, 1.0f), 1.0f);
    }
}

void game_draw_stage(Mel_Render_List* list)
{
    mel_draw_sprite(list,
        .pos = mel_vec2(0, STAGE_FLOOR_Y),
        .size = mel_vec2(GAME_W, GAME_H - STAGE_FLOOR_Y),
        .color = mel_vec4(0.25f, 0.18f, 0.12f, 1.0f));

    mel_draw_sprite(list,
        .pos = mel_vec2(0, STAGE_FLOOR_Y - 2),
        .size = mel_vec2(GAME_W, 2),
        .color = mel_vec4(0.5f, 0.35f, 0.2f, 1.0f));
}
