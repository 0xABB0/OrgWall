#pragma once

#include "core.types.h"
#include "mugen.command.h"
#include "mugen.cns.h"
#include "allocator.fwd.h"

typedef struct Fighter_Helper Fighter_Helper;
struct Fighter_Helper {
    i32 helper_id;
    f32 x, y;
    bool facing_right;

    f32 ground_front, ground_back;
    const Mel_Alloc* alloc;

    u32 current_action;

    Mugen_Char_State cns_state;
    u32 last_cns_anim;
    bool pending_destroy;
};

typedef struct Fighter Fighter;
struct Fighter {
    f32 x, y;
    f32 vel_x, vel_y;
    bool facing_right;

    f32 ground_front;
    f32 ground_back;

    Command_List commands;

    u32 current_action;

    bool input_left, input_right, input_up, input_down;
    bool btn_a, btn_b, btn_c;
    bool btn_x, btn_y, btn_z;

    Mugen_Char_State cns_state;
    Mugen_Cns* cns;
    Mugen_Cns* common_cns;
    Mugen_Cns* cmd_cns;
    u32 last_cns_anim;

    Fighter* opponent;

    f32 start_x;
    bool start_facing_right;

    Fighter_Helper* helpers;
    u32 helper_count;
    u32 helper_capacity;
    const Mel_Alloc* alloc;
};

typedef struct {
    f32 x, y, w, h;
} Fighter_Box;

typedef struct {
    f32 start_x;
    bool facing_right;
    f32 ground_front;
    f32 ground_back;
    Mugen_Air* air;
} Fighter_Init_Opt;

void fighter_init_opt(Fighter* f, Fighter_Init_Opt opt, const Mel_Alloc* alloc);
#define fighter_init(f, alloc, ...) \
    fighter_init_opt((f), (Fighter_Init_Opt){__VA_ARGS__}, (alloc))

void fighter_tick(Fighter* f, f32 dt, f32 stage_left, f32 stage_right);
void fighter_apply_combat_state(Fighter* f);

void fighter_enable_cns(Fighter* f, Mugen_Cns* cns, Mugen_Cns* common_cns, Mugen_Cns* cmd_cns);

Fighter_Box fighter_hurtbox(Fighter* f);

Fighter_Box fighter_hitbox(Fighter* f);

bool fighter_has_active_hitbox(Fighter* f);

Fighter_Box helper_hurtbox(Fighter_Helper* h);
Fighter_Box helper_hitbox(Fighter_Helper* h);
bool helper_has_active_hitbox(Fighter_Helper* h);

void fighter_round_reset(Fighter* f);
void fighter_shutdown(Fighter* f);
