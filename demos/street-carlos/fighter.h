#pragma once

#include "core.types.h"
#include "command.h"
#include "anim.player.h"
#include "anim.clip.fwd.h"
#include "mugen_cns.h"

typedef struct {
    u32 action_number;
    Mel_Anim_Clip_Handle clip;
} Fighter_Action_Map;

typedef struct {
    f32 x, y;
    f32 vel_x, vel_y;
    bool facing_right;

    f32 ground_front;
    f32 ground_back;

    Command_List commands;

    Mel_Anim_Player player;
    Mel_Anim_Clip_Pool* clip_pool;
    Fighter_Action_Map* action_map;
    u32 action_map_count;

    u32 current_action;
    f32 anim_hitbox[4];
    f32 anim_hurtbox[4];

    bool input_left, input_right, input_up, input_down;
    bool btn_a, btn_b, btn_c;
    bool btn_x, btn_y, btn_z;

    Mugen_Char_State cns_state;
    Mugen_Cns* cns;
    Mugen_Cns* common_cns;
    Mugen_Cns* cmd_cns;
    u32 last_cns_anim;
} Fighter;

typedef struct {
    f32 x, y, w, h;
} Fighter_Box;

typedef struct {
    f32 start_x;
    bool facing_right;
    f32 ground_front;
    f32 ground_back;
    Mel_Anim_Clip_Pool* clip_pool;
    Fighter_Action_Map* action_map;
    u32 action_map_count;
} Fighter_Init_Opt;

void fighter_init_opt(Fighter* f, Fighter_Init_Opt opt, const Mel_Alloc* alloc);
#define fighter_init(f, alloc, ...) \
    fighter_init_opt((f), (Fighter_Init_Opt){__VA_ARGS__}, (alloc))

void fighter_on_input(Fighter* f, u32 action, f32 value);

void fighter_tick(Fighter* f, f32 dt, f32 stage_left, f32 stage_right);
void fighter_apply_combat_state(Fighter* f);

void fighter_enable_cns(Fighter* f, Mugen_Cns* cns, Mugen_Cns* common_cns, Mugen_Cns* cmd_cns);

Fighter_Box fighter_hurtbox(Fighter* f);

Fighter_Box fighter_hitbox(Fighter* f);

bool fighter_has_active_hitbox(Fighter* f);
