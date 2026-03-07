#pragma once

#include "character.h"
#include "move.h"

static Character_Def CARLOS_DEF = {
    .name = S8("Carlos"),
    .walk_speed = 120.0f,
    .jump_vel = 350.0f,
    .gravity = 900.0f,
    .width = 32.0f,
    .height = 48.0f,
    .crouch_height = 29.0f,
    .hurt_x = 0.0f,  .hurt_y = 0.0f,  .hurt_w = 32.0f, .hurt_h = 48.0f,
    .crouch_hurt_x = -1.0f, .crouch_hurt_y = 0.0f, .crouch_hurt_w = 34.0f, .crouch_hurt_h = 29.0f,
};

static Move_Def CARLOS_MOVES[] = {
    {
        .name = S8("Light Punch"),
        .command_name = S8("LP"),
        .priority = 0,
        .requires_ground = true,
        .startup = 3,
        .active = 4,
        .recovery = 5,
        .hit_x = 32.0f, .hit_y = 14.0f, .hit_w = 16.0f, .hit_h = 8.0f,
        .damage = 5.0f,
        .knockback_x = 40.0f,
        .knockback_y = 0.0f,
        .hitstun = 8,
    },
    {
        .name = S8("Hadouken"),
        .command_name = S8("Hadouken"),
        .priority = 10,
        .requires_ground = true,
        .startup = 8,
        .active = 2,
        .recovery = 10,
        .damage = 15.0f,
        .knockback_x = 80.0f,
        .knockback_y = 0.0f,
        .hitstun = 15,
        .spawns_projectile = true,
        .projectile_speed = 200.0f,
    },
    {
        .name = S8("Shoryuken"),
        .command_name = S8("Shoryuken"),
        .priority = 20,
        .requires_ground = true,
        .startup = 2,
        .active = 10,
        .recovery = 8,
        .hit_x = 8.0f, .hit_y = 36.0f, .hit_w = 16.0f, .hit_h = 18.0f,
        .damage = 20.0f,
        .knockback_x = 30.0f,
        .knockback_y = 200.0f,
        .hitstun = 20,
        .airborne = true,
        .launch_vel_x = 80.0f,
        .launch_vel_y = 450.0f,
    },
};

#define CARLOS_MOVE_COUNT 3
