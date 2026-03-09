#include "combat.h"
#include "command.h"
#include "mugen_cns.h"
#include "string.str8.h"
#include <stdio.h>

static bool boxes_overlap(Fighter_Box a, Fighter_Box b)
{
    return a.w > 0 && a.h > 0 && b.w > 0 && b.h > 0 &&
           a.x < b.x + b.w && a.x + a.w > b.x &&
           a.y < b.y + b.h && a.y + a.h > b.y;
}

static void populate_ghv(Mugen_GetHitVar* ghv, Mugen_HitDef_Result* hd, Mugen_Char_State* victim)
{
    ghv->animtype = hd->animtype;
    ghv->groundtype = hd->ground_type;
    ghv->attr = (i32)hd->attr;
    ghv->priority = hd->priority;
    ghv->forcestand = hd->forcestand;
    ghv->damage += (i32)hd->damage_hit;
    ghv->hitcount++;
    ghv->hitshaketime = hd->pausetime_p2;

    if (victim->statetype == MUGEN_PHYSICS_A)
    {
        ghv->xvel = hd->air_vel_x;
        ghv->yvel = hd->air_vel_y;
        ghv->hittime = hd->air_hittime;
        ghv->fallflag = hd->air_fall || hd->fall;
    }
    else
    {
        ghv->xvel = hd->ground_vel_x;
        ghv->yvel = hd->ground_vel_y;
        ghv->hittime = hd->ground_hittime;
        ghv->slidetime = hd->ground_slidetime;
        ghv->fallflag = hd->fall;
    }

    ghv->ctrltime = ghv->hittime;
    ghv->yaccel = hd->has_yaccel ? hd->yaccel : victim->gravity;

    ghv->fall_recover = hd->fall_recover;
    ghv->fall_recovertime = hd->fall_recovertime;
    ghv->fall_xvel = hd->fall_vel_x;
    ghv->fall_yvel = hd->fall_vel_y;
}

static i32 compute_hit_state(Mugen_Char_State* victim, Mugen_HitDef_Result* hd)
{
    u8 animtype = hd->animtype;

    if (victim->statetype == MUGEN_PHYSICS_A)
        return 5020 + animtype;

    if (victim->statetype == MUGEN_PHYSICS_C)
        return 5010 + animtype;

    if (hd->ground_type == MUGEN_GROUNDTYPE_LOW)
        return 5010 + animtype;

    return 5000 + animtype;
}

static bool can_guard(Mugen_Char_State* vst, Mugen_HitDef_Result* hd)
{
    if (!vst->commands) return false;
    if (!command_list_active(vst->commands, S8("holdback"))) return false;
    if (hd->guardflag == 0) return false;

    if (vst->statetype == MUGEN_PHYSICS_S && (hd->guardflag & MUGEN_HF_H)) return true;
    if (vst->statetype == MUGEN_PHYSICS_C && (hd->guardflag & MUGEN_HF_L)) return true;
    if (vst->statetype == MUGEN_PHYSICS_A && (hd->guardflag & MUGEN_HF_A)) return true;
    return false;
}

static i32 compute_guard_state(Mugen_Char_State* victim)
{
    if (victim->statetype == MUGEN_PHYSICS_A) return 154;
    if (victim->statetype == MUGEN_PHYSICS_C) return 152;
    return 150;
}

static void apply_guard(Fighter* attacker, Fighter* victim)
{
    Mugen_Char_State* ast = &attacker->cns_state;
    Mugen_Char_State* vst = &victim->cns_state;
    Mugen_HitDef_Result* hd = &ast->hitdef;

    vst->ghv.guarded = true;
    vst->ghv.hitshaketime = hd->guard_pausetime_p2;
    vst->ghv.xvel = hd->guard_velocity;
    vst->ghv.yvel = 0;
    vst->ghv.slidetime = hd->guard_slidetime;
    vst->ghv.hittime = hd->guard_slidetime;
    vst->ghv.ctrltime = hd->guard_ctrltime;
    vst->ghv.damage += (i32)hd->damage_guard;
    vst->ghv.guardcount++;

    f32 gdmg = hd->damage_guard * (vst->defence_mul > 0 ? vst->defence_mul : 1.0f);
    vst->life -= gdmg;
    if (vst->life < 1) vst->life = 1;

    ast->hitpause_time = hd->guard_pausetime_p1;
    vst->hitpause_time = hd->guard_pausetime_p2;

    ast->moveguarded = 1;
    ast->mctime = 1;

    i32 guard_state = compute_guard_state(vst);

    vst->movetype = MUGEN_MOVETYPE_H;
    vst->ctrl = false;

    vst->pending_state = guard_state;
    vst->pending_ctrl = 0;
    vst->state_changed = true;

    ast->hitdef_pending = false;
    ast->hitdef_active = false;

    bool victim_in_corner = (vst->pos_x <= vst->stage_left + vst->ground_back + 1.0f) ||
                            (vst->pos_x >= vst->stage_right - vst->ground_front - 1.0f);
    if (victim_in_corner)
    {
        attacker->x += hd->guard_cornerpush_veloff * ast->facing;
        ast->pos_x = attacker->x;
    }

    printf("GUARD: attacker state=%d -> victim state=%d (damage=%.0f life=%.0f pause=%d/%d)\n",
        ast->stateno, guard_state, hd->damage_guard, vst->life,
        hd->pausetime_p1, hd->pausetime_p2);
}

static void apply_hit(Fighter* attacker, Fighter* victim)
{
    Mugen_Char_State* ast = &attacker->cns_state;
    Mugen_Char_State* vst = &victim->cns_state;
    Mugen_HitDef_Result* hd = &ast->hitdef;

    populate_ghv(&vst->ghv, hd, vst);

    f32 dmg = hd->damage_hit * (vst->defence_mul > 0 ? vst->defence_mul : 1.0f);
    vst->life -= dmg;
    if (vst->life < 0) vst->life = 0;

    ast->hitpause_time = hd->pausetime_p1;
    vst->hitpause_time = hd->pausetime_p2;

    ast->movehit = 1;
    ast->mctime = 1;
    ast->hitcount++;
    ast->target = vst;

    if (vst->statetype == MUGEN_PHYSICS_A)
        vst->juggle_points_remaining -= hd->juggle;

    i32 hit_state = compute_hit_state(vst, hd);

    if (hd->p2stateno >= 0)
        hit_state = hd->p2stateno;

    vst->movetype = MUGEN_MOVETYPE_H;
    vst->ctrl = false;

    vst->pending_state = hit_state;
    vst->pending_ctrl = 0;
    vst->state_changed = true;

    if (hd->p2stateno >= 0 && hd->p2getp1state)
    {
        vst->state_owner_cns = ast->self_cns;
        vst->bound_to = ast;
    }

    if (hd->p1stateno >= 0)
    {
        ast->pending_state = hd->p1stateno;
        ast->pending_ctrl = 0;
        ast->state_changed = true;
    }

    if (hd->p1facing != 0)
    {
        bool face_right = (victim->x > attacker->x);
        if (hd->p1facing < 0) face_right = !face_right;
        ast->facing = face_right ? 1.0f : -1.0f;
        attacker->facing_right = face_right;
        command_list_set_facing(&attacker->commands, face_right);
    }

    if (hd->p2facing != 0)
    {
        bool face_right = (attacker->x > victim->x);
        if (hd->p2facing < 0) face_right = !face_right;
        vst->facing = face_right ? 1.0f : -1.0f;
        victim->facing_right = face_right;
        command_list_set_facing(&victim->commands, face_right);
    }

    ast->hitdef_pending = false;
    ast->hitdef_active = false;

    bool victim_in_corner = (vst->pos_x <= vst->stage_left + vst->ground_back + 1.0f) ||
                            (vst->pos_x >= vst->stage_right - vst->ground_front - 1.0f);
    if (victim_in_corner)
    {
        f32 push = (vst->statetype == MUGEN_PHYSICS_A)
            ? hd->air_cornerpush_veloff
            : hd->ground_cornerpush_veloff;
        attacker->x += push * ast->facing;
        ast->pos_x = attacker->x;
    }

    printf("HIT: attacker state=%d -> victim state=%d (damage=%.0f life=%.0f)\n",
        ast->stateno, hit_state, hd->damage_hit, vst->life);
}

static void check_hit(Fighter* attacker, Fighter* victim)
{
    Mugen_Char_State* ast = &attacker->cns_state;
    Mugen_Char_State* vst = &victim->cns_state;

    if (!ast->hitdef_pending && !ast->hitdef_active) return;
    if (!fighter_has_active_hitbox(attacker)) return;
    if (vst->hitpause_time > 0) return;

    if (vst->nothitby_time > 0 && (vst->nothitby_attr & ast->hitdef.attr))
        return;

    {
        u32 hf = ast->hitdef.hitflag;
        bool is_hit = (vst->movetype == MUGEN_MOVETYPE_H);
        if (hf & MUGEN_HF_MNS)
        {
            if (is_hit) return;
        }
        else if (hf & MUGEN_HF_PLS)
        {
            if (!is_hit) return;
        }

        if (!(hf & MUGEN_HF_MNS))
        {
            if (vst->statetype == MUGEN_PHYSICS_S && !(hf & MUGEN_HF_H)) return;
            if (vst->statetype == MUGEN_PHYSICS_C && !(hf & MUGEN_HF_L)) return;
            if (vst->statetype == MUGEN_PHYSICS_A && !(hf & MUGEN_HF_A)) return;
            if (vst->statetype == MUGEN_PHYSICS_L && !(hf & MUGEN_HF_D)) return;
        }
    }

    if (vst->statetype == MUGEN_PHYSICS_A && !(ast->assert_flags & MUGEN_ASSERT_NOJUGGLECHECK))
    {
        if (vst->juggle_points_remaining < ast->hitdef.juggle)
            return;
    }

    Fighter_Box atk_box = fighter_hitbox(attacker);
    Fighter_Box def_box = fighter_hurtbox(victim);

    if (!boxes_overlap(atk_box, def_box)) return;

    if (ast->hitdef_pending)
    {
        ast->hitdef_active = true;
        ast->hitdef_pending = false;
    }

    if (can_guard(vst, &ast->hitdef))
        apply_guard(attacker, victim);
    else
        apply_hit(attacker, victim);
}

static void check_helper_vs_fighter(Fighter_Helper* h, Fighter* victim)
{
    Mugen_Char_State* ast = &h->cns_state;
    Mugen_Char_State* vst = &victim->cns_state;

    if (!ast->hitdef_pending && !ast->hitdef_active) return;
    if (!helper_has_active_hitbox(h)) return;
    if (vst->hitpause_time > 0) return;

    if (vst->nothitby_time > 0 && (vst->nothitby_attr & ast->hitdef.attr))
        return;

    if (vst->statetype == MUGEN_PHYSICS_A && !(ast->assert_flags & MUGEN_ASSERT_NOJUGGLECHECK))
    {
        if (vst->juggle_points_remaining < ast->hitdef.juggle)
            return;
    }

    Fighter_Box atk_box = helper_hitbox(h);
    Fighter_Box def_box = fighter_hurtbox(victim);

    if (!boxes_overlap(atk_box, def_box)) return;

    if (ast->hitdef_pending)
    {
        ast->hitdef_active = true;
        ast->hitdef_pending = false;
    }

    Mugen_HitDef_Result* hd = &ast->hitdef;

    populate_ghv(&vst->ghv, hd, vst);

    f32 dmg = hd->damage_hit * (vst->defence_mul > 0 ? vst->defence_mul : 1.0f);
    vst->life -= dmg;
    if (vst->life < 0) vst->life = 0;

    ast->hitpause_time = hd->pausetime_p1;
    vst->hitpause_time = hd->pausetime_p2;

    ast->movehit = 1;
    ast->mctime = 1;
    ast->hitcount++;

    i32 hit_state = compute_hit_state(vst, hd);
    if (hd->p2stateno >= 0)
        hit_state = hd->p2stateno;

    vst->movetype = MUGEN_MOVETYPE_H;
    vst->ctrl = false;
    vst->pending_state = hit_state;
    vst->pending_ctrl = 0;
    vst->state_changed = true;

    ast->hitdef_pending = false;
    ast->hitdef_active = false;

    printf("HELPER HIT: helper_id=%d -> victim state=%d (damage=%.0f life=%.0f)\n",
        h->helper_id, hit_state, hd->damage_hit, vst->life);
}

static void auto_face(Fighter* f, Fighter* opponent)
{
    Mugen_Char_State* st = &f->cns_state;
    if (!st->ctrl) return;
    if (st->movetype != MUGEN_MOVETYPE_I) return;
    if (st->statetype == MUGEN_PHYSICS_A) return;
    if (st->assert_flags & MUGEN_ASSERT_NOAUTOTURN) return;

    bool should_face_right = (opponent->x > f->x);
    bool currently_right = (st->facing > 0);
    if (should_face_right != currently_right)
    {
        st->facing = should_face_right ? 1.0f : -1.0f;
        f->facing_right = should_face_right;
        command_list_set_facing(&f->commands, should_face_right);
    }
}

void combat_resolve(Fighter* f1, Fighter* f2)
{
    f1->opponent = f2;
    f2->opponent = f1;

    Mugen_Char_State* s1 = &f1->cns_state;
    Mugen_Char_State* s2 = &f2->cns_state;

    s1->p2_pos_x = f2->x;
    s1->p2_pos_y = -f2->y;
    s1->p2_statetype = s2->statetype;
    s1->p2_movetype = s2->movetype;
    s1->p2_width = f2->ground_front;
    s2->p2_pos_x = f1->x;
    s2->p2_pos_y = -f1->y;
    s2->p2_statetype = s1->statetype;
    s2->p2_movetype = s1->movetype;
    s2->p2_width = f1->ground_front;

    auto_face(f1, f2);
    auto_face(f2, f1);

    if (s1->statetype != MUGEN_PHYSICS_A && s2->statetype != MUGEN_PHYSICS_A)
    {
        f32 f1_left  = f1->x - f1->ground_back;
        f32 f1_right = f1->x + f1->ground_front;
        f32 f2_left  = f2->x - f2->ground_back;
        f32 f2_right = f2->x + f2->ground_front;

        if (f1_right > f2_left && f1_left < f2_right)
        {
            f32 overlap = 0;
            if (f1->x < f2->x)
                overlap = f1_right - f2_left;
            else
                overlap = f2_right - f1_left;

            f32 half = overlap * 0.5f;
            if (f1->x < f2->x)
            {
                f1->x -= half;
                f2->x += half;
            }
            else
            {
                f1->x += half;
                f2->x -= half;
            }

            s1->pos_x = f1->x;
            s2->pos_x = f2->x;
        }
    }

    check_hit(f1, f2);
    check_hit(f2, f1);

    for (u32 i = 0; i < f1->helper_count; i++)
        check_helper_vs_fighter(&f1->helpers[i], f2);
    for (u32 i = 0; i < f2->helper_count; i++)
        check_helper_vs_fighter(&f2->helpers[i], f1);
}
