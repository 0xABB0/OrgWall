#include "combat.h"

bool boxes_overlap(Fighter_Box a, Fighter_Box b)
{
    if (a.w <= 0 || a.h <= 0 || b.w <= 0 || b.h <= 0) return false;

    return a.x < b.x + b.w
        && a.x + a.w > b.x
        && a.y < b.y + b.h
        && a.y + a.h > b.y;
}

void combat_check_hits(Fighter* attacker, Fighter* defender)
{
    if (!fighter_has_active_hitbox(attacker)) return;

    Fighter_Box hit = fighter_hitbox(attacker);
    Fighter_Box hurt = fighter_hurtbox(defender);

    if (boxes_overlap(hit, hurt))
    {
        attacker->hit_confirmed = true;
        Move_Def* m = attacker->current_move;

        f32 kb_x = attacker->facing_right ? m->knockback_x : -m->knockback_x;
        fighter_take_hit(defender, m->damage, kb_x, m->knockback_y, m->hitstun);
    }
}

void combat_check_projectiles(Fighter* shooter, Fighter* target)
{
    Fighter_Box hurt = fighter_hurtbox(target);

    for (u32 i = 0; i < MAX_PROJECTILES; i++)
    {
        Projectile* p = &shooter->projectiles[i];
        if (!p->active) continue;

        Fighter_Box proj_box = {
            .x = p->x,
            .y = p->y,
            .w = p->hit_w,
            .h = p->hit_h,
        };

        if (boxes_overlap(proj_box, hurt))
        {
            p->active = false;
            f32 kb_dir = p->vel_x > 0 ? 1.0f : -1.0f;
            fighter_take_hit(target, p->damage, 60.0f * kb_dir, 0.0f, p->hitstun);
        }
    }
}
