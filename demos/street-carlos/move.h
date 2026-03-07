#pragma once

#include "core.types.h"
#include "string.str8.h"

typedef u32 Move_Phase;
#define MOVE_PHASE_NONE     0
#define MOVE_PHASE_STARTUP  1
#define MOVE_PHASE_ACTIVE   2
#define MOVE_PHASE_RECOVERY 3

typedef struct {
    str8 name;

    str8 command_name;
    u32 priority;

    bool requires_ground;
    bool requires_air;
    bool requires_crouch;

    u32 startup;
    u32 active;
    u32 recovery;

    f32 hit_x, hit_y, hit_w, hit_h;

    f32 damage;
    f32 knockback_x;
    f32 knockback_y;
    u32 hitstun;

    bool airborne;
    f32 launch_vel_x;
    f32 launch_vel_y;

    bool spawns_projectile;
    f32 projectile_speed;
} Move_Def;

static inline u32 move_duration(const Move_Def* m)
{
    return m->startup + m->active + m->recovery;
}

static inline Move_Phase move_phase_at_frame(const Move_Def* m, u32 frame)
{
    if (frame < m->startup) return MOVE_PHASE_STARTUP;
    if (frame < m->startup + m->active) return MOVE_PHASE_ACTIVE;
    if (frame < move_duration(m)) return MOVE_PHASE_RECOVERY;
    return MOVE_PHASE_NONE;
}
