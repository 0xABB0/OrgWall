#pragma once

#include "core.types.h"
#include "string.str8.h"

typedef u32 Locomotion;
#define LOCO_IDLE          0
#define LOCO_WALK_FORWARD  1
#define LOCO_WALK_BACK     2
#define LOCO_CROUCH        3
#define LOCO_JUMP          4
#define LOCO_HITSTUN       5

typedef struct {
    str8 name;

    f32 walk_speed;
    f32 jump_vel;
    f32 gravity;

    f32 width;
    f32 height;
    f32 crouch_height;

    f32 hurt_x, hurt_y, hurt_w, hurt_h;
    f32 crouch_hurt_x, crouch_hurt_y, crouch_hurt_w, crouch_hurt_h;
} Character_Def;
