#pragma once

#include "core.types.h"
#include "command.h"
#include "move.h"
#include "character.h"

#define MAX_PROJECTILES 4

typedef struct {
    f32 x, y;
    f32 vel_x;
    f32 hit_w, hit_h;
    f32 damage;
    u32 hitstun;
    bool active;
} Projectile;

typedef struct {
    Character_Def* character;

    Locomotion locomotion;

    Move_Def* current_move;
    Move_Phase move_phase;
    u32 move_frame;
    bool hit_confirmed;

    f32 x, y;
    f32 vel_x, vel_y;
    bool facing_right;

    f32 health;
    u32 hitstun_remaining;

    Command_List commands;
    Projectile projectiles[MAX_PROJECTILES];

    Move_Def* moves;
    u32 move_count;

    bool input_left, input_right, input_up, input_down;
    bool btn_a, btn_b, btn_c;
    bool btn_x, btn_y, btn_z;
} Fighter;

typedef struct {
    f32 x, y, w, h;
} Fighter_Box;

void fighter_init(Fighter* f, Character_Def* character, Move_Def* moves, u32 move_count,
                  f32 start_x, bool facing_right, const Mel_Alloc* alloc);

void fighter_on_input(Fighter* f, u32 action, f32 value);

void fighter_tick(Fighter* f, f32 dt, f32 stage_left, f32 stage_right);

void fighter_take_hit(Fighter* f, f32 damage, f32 knockback_x, f32 knockback_y, u32 hitstun);

Fighter_Box fighter_hurtbox(Fighter* f);

Fighter_Box fighter_hitbox(Fighter* f);

bool fighter_has_active_hitbox(Fighter* f);
