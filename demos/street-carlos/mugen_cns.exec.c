#include "mugen_cns.h"
#include "string.str8.h"
#include <string.h>
#include <stdlib.h>

static u64 mugen_rng_next(u64* s)
{
    u64 x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *s = x;
    return x;
}

static bool str8_ieq(str8 a, const char* b)
{
    size blen = (size)strlen(b);
    if (a.len != blen) return false;
    for (size i = 0; i < blen; i++)
    {
        u8 ac = a.data[i]; if (ac >= 'A' && ac <= 'Z') ac += 32;
        u8 bc = (u8)b[i]; if (bc >= 'A' && bc <= 'Z') bc += 32;
        if (ac != bc) return false;
    }
    return true;
}

void mugen_targets_add(Mugen_Char_State* state, Mugen_Char_State* target, i32 hitdef_id)
{
    for (u32 i = 0; i < state->target_count; i++)
    {
        if (state->targets[i].state == target)
            return;
    }
    if (state->target_count >= state->target_cap)
    {
        u32 new_cap = state->target_cap < 4 ? 4 : state->target_cap * 2;
        Mugen_Target_Entry* buf = realloc(state->targets, new_cap * sizeof(Mugen_Target_Entry));
        state->targets = buf;
        state->target_cap = new_cap;
    }
    state->targets[state->target_count++] = (Mugen_Target_Entry){ .state = target, .hitdef_id = hitdef_id };
}

void mugen_targets_clear(Mugen_Char_State* state)
{
    state->target_count = 0;
}

void mugen_targets_free(Mugen_Char_State* state)
{
    free(state->targets);
    state->targets = NULL;
    state->target_count = 0;
    state->target_cap = 0;
}

void mugen_cns_enter_state(Mugen_Cns* cns, Mugen_Char_State* state, i32 stateno)
{
    Mugen_Statedef* def = mugen_cns_get(cns, stateno);
    if (!def) return;

    state->prevstateno = state->stateno;
    state->stateno = stateno;
    state->time = 0;
    state->state_changed = false;
    if (!def->hitdefpersist)
    {
        state->hitdef_pending = false;
        state->hitdef_active = false;
    }
    if (!def->movehitpersist)
    {
        state->mctime = 0;
        state->movehit = 0;
        state->moveguarded = 0;
    }
    if (!def->hitcountpersist)
        state->hitcount = 0;

    {
        u8 st = def->statetype ? def->statetype : MUGEN_PHYSICS_S;
        if (st != MUGEN_STATETYPE_U) state->statetype = st;
    }
    {
        u8 mt = (def->movetype == 0xFF) ? MUGEN_MOVETYPE_I : def->movetype;
        if (mt != MUGEN_MOVETYPE_U)
        {
            if (state->movetype == MUGEN_MOVETYPE_H && mt != MUGEN_MOVETYPE_H)
                state->juggle_points_remaining = (i32)state->data_airjuggle;
            state->movetype = mt;
        }
    }
    {
        u8 ph = def->physics ? def->physics : MUGEN_PHYSICS_N;
        if (ph != MUGEN_PHYSICS_U) state->physics = ph;
    }
    if (def->anim >= 0)
    {
        state->anim = (u32)def->anim;
        state->pending_anim = def->anim;
        state->animtime = -999;
        state->animelem = 0;
        state->animelemtime = 0;
    }
    if (def->has_velset)
    {
        state->vel_x = def->velset_x;
        state->vel_y = def->velset_y;
    }
    if (def->ctrl >= 0)
        state->ctrl = def->ctrl != 0;

    state->current_juggle = def->juggle;

    if (def->poweradd != 0)
    {
        state->power += (f32)def->poweradd;
        if (state->power > state->powermax) state->power = state->powermax;
        if (state->power < 0) state->power = 0;
    }

    state->sprpriority = def->sprpriority;

    for (u32 i = 0; i < def->controller_count; i++)
        def->controllers[i].persistent_counter = 0;
}

static bool eval_triggers(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    for (u32 i = 0; i < sc->triggerall_count; i++)
    {
        if (!mugen_expr_eval_bool(sc->triggerall[i], state))
            return false;
    }

    if (sc->trigger_group_count == 0)
        return sc->triggerall_count > 0;

    for (u32 g = 0; g < sc->trigger_group_count; g++)
    {
        Mugen_Trigger_Group* grp = &sc->trigger_groups[g];
        bool all_true = true;
        for (u32 c = 0; c < grp->count; c++)
        {
            if (!mugen_expr_eval_bool(grp->conditions[c], state))
            {
                all_true = false;
                break;
            }
        }
        if (all_true && grp->count > 0)
            return true;
    }

    return false;
}

static f32 eval_or_default(Mugen_Expr* e, Mugen_Char_State* st, f32 def)
{
    return e ? mugen_expr_eval(e, st) : def;
}

static void exec_controller(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    switch (sc->type)
    {
        case MUGEN_SC_NULL:
        case MUGEN_SC_AFTERIMAGE:
        case MUGEN_SC_AFTERIMAGETIME:
        case MUGEN_SC_FALLENVSHAKE:
            break;

        case MUGEN_SC_SPRPRIORITY:
        {
            Mugen_SprPriority_Params* p = (Mugen_SprPriority_Params*)sc->params;
            state->sprpriority = (i32)mugen_expr_eval(p->value, state);
            break;
        }

        case MUGEN_SC_TURN:
            state->facing = -state->facing;
            break;

        case MUGEN_SC_STATETYPESET:
        {
            Mugen_StateTypeSet_Params* p = sc->params;
            if (!p) break;
            if (p->statetype >= 0) state->statetype = (u8)p->statetype;
            if (p->movetype >= 0) state->movetype = (u8)p->movetype;
            if (p->physics >= 0) state->physics = (u8)p->physics;
            break;
        }

        case MUGEN_SC_VELSET:
        {
            Mugen_Vel_Params* p = sc->params;
            if (!p) break;
            if (p->x) state->vel_x = mugen_expr_eval(p->x, state);
            if (p->y) state->vel_y = mugen_expr_eval(p->y, state);
            break;
        }
        case MUGEN_SC_VELADD:
        {
            Mugen_Vel_Params* p = sc->params;
            if (!p) break;
            if (p->x) state->vel_x += mugen_expr_eval(p->x, state);
            if (p->y) state->vel_y += mugen_expr_eval(p->y, state);
            break;
        }
        case MUGEN_SC_VELMUL:
        {
            Mugen_Vel_Params* p = sc->params;
            if (!p) break;
            if (p->x) state->vel_x *= mugen_expr_eval(p->x, state);
            if (p->y) state->vel_y *= mugen_expr_eval(p->y, state);
            break;
        }
        case MUGEN_SC_POSSET:
        {
            Mugen_Pos_Params* p = sc->params;
            if (!p) break;
            if (p->x) state->pos_x = mugen_expr_eval(p->x, state);
            if (p->y) state->pos_y = mugen_expr_eval(p->y, state);
            break;
        }
        case MUGEN_SC_POSADD:
        {
            Mugen_Pos_Params* p = sc->params;
            if (!p) break;
            if (p->x) state->pos_x += mugen_expr_eval(p->x, state) * state->facing;
            if (p->y) state->pos_y += mugen_expr_eval(p->y, state);
            break;
        }
        case MUGEN_SC_CHANGESTATE:
        {
            Mugen_ChangeState_Params* p = sc->params;
            if (!p || !p->value) break;
            state->pending_state = (i32)mugen_expr_eval(p->value, state);
            if (p->ctrl) state->pending_ctrl = (i32)mugen_expr_eval(p->ctrl, state);
            if (p->anim) state->pending_anim = (i32)mugen_expr_eval(p->anim, state);
            state->state_changed = true;
            break;
        }
        case MUGEN_SC_CHANGEANIM:
        {
            Mugen_ChangeAnim_Params* p = sc->params;
            if (!p || !p->value) break;
            state->pending_anim = (i32)mugen_expr_eval(p->value, state);
            state->anim = (u32)state->pending_anim;
            state->animelem = 0;
            state->animtime = -999;
            state->animelemtime = 0;
            break;
        }
        case MUGEN_SC_CTRL:
        {
            Mugen_Ctrl_Params* p = sc->params;
            if (!p || !p->value) break;
            state->ctrl = mugen_expr_eval(p->value, state) != 0.0f;
            break;
        }
        case MUGEN_SC_GRAVITY:
        {
            state->vel_y += state->gravity;
            break;
        }
        case MUGEN_SC_VARSET:
        {
            Mugen_VarSet_Params* p = sc->params;
            if (!p || !p->value) break;
            i32 idx = p->index ? (i32)mugen_expr_eval(p->index, state) : 0;
            f32 val = mugen_expr_eval(p->value, state);
            switch (p->var_type)
            {
                case MUGEN_VAR_INT:
                    if (idx >= 0 && idx < 60) state->var[idx] = (i32)val;
                    break;
                case MUGEN_VAR_FLOAT:
                    if (idx >= 0 && idx < 40) state->fvar[idx] = val;
                    break;
                case MUGEN_VAR_SYSINT:
                    if (idx >= 0 && idx < 5) state->sysvar[idx] = (i32)val;
                    break;
                case MUGEN_VAR_SYSFLOAT:
                    if (idx >= 0 && idx < 5) state->sysfvar[idx] = val;
                    break;
            }
            break;
        }
        case MUGEN_SC_VARADD:
        {
            Mugen_VarSet_Params* p = sc->params;
            if (!p || !p->value) break;
            i32 idx = p->index ? (i32)mugen_expr_eval(p->index, state) : 0;
            f32 val = mugen_expr_eval(p->value, state);
            switch (p->var_type)
            {
                case MUGEN_VAR_INT:
                    if (idx >= 0 && idx < 60) state->var[idx] += (i32)val;
                    break;
                case MUGEN_VAR_FLOAT:
                    if (idx >= 0 && idx < 40) state->fvar[idx] += val;
                    break;
                case MUGEN_VAR_SYSINT:
                    if (idx >= 0 && idx < 5) state->sysvar[idx] += (i32)val;
                    break;
                case MUGEN_VAR_SYSFLOAT:
                    if (idx >= 0 && idx < 5) state->sysfvar[idx] += val;
                    break;
            }
            break;
        }
        case MUGEN_SC_VARRANDOM:
        {
            Mugen_VarRandom_Params* p = sc->params;
            if (!p) break;
            i32 idx = p->index ? (i32)mugen_expr_eval(p->index, state) : 0;
            i32 lo = p->range_low ? (i32)mugen_expr_eval(p->range_low, state) : 0;
            i32 hi = p->range_high ? (i32)mugen_expr_eval(p->range_high, state) : 999;
            i32 val = (hi > lo) ? lo + (i32)(mugen_rng_next(&state->rng_state) % (u64)(hi - lo + 1)) : lo;
            if (p->var_type == MUGEN_VAR_INT && idx >= 0 && idx < 60)
                state->var[idx] = val;
            break;
        }
        case MUGEN_SC_HITDEF:
        {
            Mugen_HitDef_Params* p = sc->params;
            if (!p) break;
            Mugen_HitDef_Result* r = &state->hitdef;
            *r = (Mugen_HitDef_Result){0};
            r->active = true;
            r->attr = p->attr;
            r->hitflag = p->hitflag ? p->hitflag : (MUGEN_HF_M | MUGEN_HF_A | MUGEN_HF_F);
            r->guardflag = p->guardflag;
            r->ground_type = p->ground_type;
            r->animtype = p->animtype;
            r->damage_hit = eval_or_default(p->damage_hit, state, 0);
            r->damage_guard = eval_or_default(p->damage_guard, state, 0);
            r->pausetime_p1 = (i32)eval_or_default(p->pausetime_p1, state, 0);
            r->pausetime_p2 = (i32)eval_or_default(p->pausetime_p2, state, 0);
            r->guard_pausetime_p1 = p->guard_pausetime_p1 ? (i32)mugen_expr_eval(p->guard_pausetime_p1, state) : r->pausetime_p1;
            r->guard_pausetime_p2 = p->guard_pausetime_p2 ? (i32)mugen_expr_eval(p->guard_pausetime_p2, state) : r->pausetime_p2;
            r->spark_x = eval_or_default(p->spark_x, state, 0);
            r->spark_y = eval_or_default(p->spark_y, state, 0);
            r->hitsound_group = (i32)eval_or_default(p->hitsound_group, state, 0);
            r->hitsound_index = (i32)eval_or_default(p->hitsound_index, state, 0);
            r->guardsound_group = (i32)eval_or_default(p->guardsound_group, state, 0);
            r->guardsound_index = (i32)eval_or_default(p->guardsound_index, state, 0);
            r->ground_slidetime = (i32)eval_or_default(p->ground_slidetime, state, 0);
            r->ground_hittime = (i32)eval_or_default(p->ground_hittime, state, 0);
            r->ground_vel_x = eval_or_default(p->ground_vel_x, state, 0);
            r->ground_vel_y = eval_or_default(p->ground_vel_y, state, 0);
            r->guard_velocity = eval_or_default(p->guard_velocity, state, r->ground_vel_x * -0.5f);
            r->guard_slidetime = (i32)eval_or_default(p->guard_slidetime, state, (f32)r->ground_slidetime);
            r->guard_hittime = (i32)eval_or_default(p->guard_hittime, state, (f32)r->guard_slidetime);
            r->guard_ctrltime = (i32)eval_or_default(p->guard_ctrltime, state, (f32)r->guard_slidetime);
            r->air_vel_x = eval_or_default(p->air_vel_x, state, 0);
            r->air_vel_y = eval_or_default(p->air_vel_y, state, 0);
            r->air_hittime = (i32)eval_or_default(p->air_hittime, state, 0);
            r->air_fall = eval_or_default(p->air_fall, state, 0) != 0.0f;
            r->fall = eval_or_default(p->fall, state, 0) != 0.0f;
            r->fall_recover = eval_or_default(p->fall_recover, state, 1) != 0.0f;
            r->fall_recovertime = (i32)eval_or_default(p->fall_recovertime, state, 4);
            r->fall_vel_x = eval_or_default(p->fall_vel_x, state, 0);
            r->fall_vel_y = eval_or_default(p->fall_vel_y, state, -4.5f);
            r->priority = (i32)eval_or_default(p->priority, state, 4);
            r->getpower_hit = (i32)eval_or_default(p->getpower_hit, state, 0);
            r->getpower_guard = (i32)eval_or_default(p->getpower_guard, state, 0);
            r->forcestand = eval_or_default(p->forcestand, state, 0) != 0.0f;
            r->hitonce = eval_or_default(p->hitonce, state, 0) != 0.0f;
            r->numhits = (i32)eval_or_default(p->numhits, state, 1);
            r->p1stateno = (i32)eval_or_default(p->p1stateno, state, -1);
            r->p2stateno = (i32)eval_or_default(p->p2stateno, state, -1);
            r->p2getp1state = eval_or_default(p->p2getp1state, state, 1) != 0.0f;
            r->p1facing = (i32)eval_or_default(p->p1facing, state, 0);
            r->p2facing = (i32)eval_or_default(p->p2facing, state, 0);
            r->juggle = state->current_juggle;
            r->ground_cornerpush_veloff = eval_or_default(p->ground_cornerpush, state, r->guard_velocity * -1.3f);
            r->air_cornerpush_veloff = eval_or_default(p->air_cornerpush, state, r->ground_cornerpush_veloff);
            r->guard_cornerpush_veloff = eval_or_default(p->guard_cornerpush, state, r->ground_cornerpush_veloff);
            if (p->yaccel)
            {
                r->yaccel = mugen_expr_eval(p->yaccel, state);
                r->has_yaccel = true;
            }
            state->hitdef_pending = true;
            break;
        }
        case MUGEN_SC_SELFSTATE:
        {
            Mugen_SelfState_Params* p = sc->params;
            if (!p || !p->value) break;
            state->pending_state = (i32)mugen_expr_eval(p->value, state);
            if (p->ctrl) state->pending_ctrl = (i32)mugen_expr_eval(p->ctrl, state);
            if (p->anim) state->pending_anim = (i32)mugen_expr_eval(p->anim, state);
            state->state_changed = true;
            state->state_owner_cns = NULL;
            state->bound_to = NULL;
            state->use_owner_anim = false;
            break;
        }
        case MUGEN_SC_HITVELSET:
        {
            Mugen_HitVelSet_Params* p = sc->params;
            if (!p) break;
            if (p->x && mugen_expr_eval(p->x, state) != 0.0f)
                state->vel_x = state->ghv.xvel;
            if (p->y && mugen_expr_eval(p->y, state) != 0.0f)
                state->vel_y = state->ghv.yvel;
            break;
        }
        case MUGEN_SC_HITFALLVEL:
        {
            state->vel_x = state->ghv.fall_xvel;
            state->vel_y = state->ghv.fall_yvel;
            break;
        }
        case MUGEN_SC_HITFALLDAMAGE:
        {
            state->life -= (f32)state->ghv.fall_damage;
            if (state->life < 0) state->life = 0;
            break;
        }
        case MUGEN_SC_POSFREEZE:
        {
            state->pos_freeze = true;
            break;
        }
        case MUGEN_SC_HITFALLSET:
        {
            Mugen_HitFallSet_Params* p = sc->params;
            if (!p) break;
            if (p->xvel_set) state->ghv.fall_xvel = mugen_expr_eval(p->fall_xvel, state);
            if (p->yvel_set) state->ghv.fall_yvel = mugen_expr_eval(p->fall_yvel, state);
            if (p->fall_recover) state->ghv.fall_recover = mugen_expr_eval(p->fall_recover, state) != 0.0f;
            if (p->fall_recovertime) state->ghv.fall_recovertime = (i32)mugen_expr_eval(p->fall_recovertime, state);
            if (p->fall_damage) state->ghv.fall_damage = (i32)mugen_expr_eval(p->fall_damage, state);
            break;
        }
        case MUGEN_SC_LIFESET:
        {
            Mugen_PowerAdd_Params* p = sc->params;
            if (!p || !p->value) break;
            state->life = mugen_expr_eval(p->value, state);
            if (state->life > state->lifemax) state->life = state->lifemax;
            if (state->life < 0) state->life = 0;
            break;
        }
        case MUGEN_SC_VARRANGESET:
        {
            Mugen_VarRangeSet_Params* p = sc->params;
            if (!p) break;
            i32 first = p->first ? (i32)mugen_expr_eval(p->first, state) : 0;
            i32 last_int = p->last ? (i32)mugen_expr_eval(p->last, state) : 59;
            i32 last_flt = p->last ? (i32)mugen_expr_eval(p->last, state) : 39;
            if (p->value)
            {
                i32 val = (i32)mugen_expr_eval(p->value, state);
                for (i32 i = first; i <= last_int && i < 60; i++)
                    if (i >= 0) state->var[i] = val;
            }
            if (p->fvalue)
            {
                f32 val = mugen_expr_eval(p->fvalue, state);
                for (i32 i = first; i <= last_flt && i < 40; i++)
                    if (i >= 0) state->fvar[i] = val;
            }
            break;
        }
        case MUGEN_SC_ASSERTSPECIAL:
        {
            Mugen_AssertSpecial_Params* p = sc->params;
            if (p) state->assert_flags |= p->flags;
            break;
        }
        case MUGEN_SC_DEFENCEMULSET:
        {
            Mugen_DefenceMulSet_Params* p = sc->params;
            if (p && p->value)
                state->defence_mul = mugen_expr_eval(p->value, state);
            break;
        }
        case MUGEN_SC_PLAYSND:
            break;
        case MUGEN_SC_NOTHITBY:
        {
            Mugen_NotHitBy_Params* p = sc->params;
            if (!p) break;
            state->nothitby_attr = p->attr_flags;
            state->nothitby_time = p->time ? (i32)mugen_expr_eval(p->time, state) : 1;
            break;
        }
        case MUGEN_SC_WIDTH:
        {
            Mugen_Width_Params* p = sc->params;
            if (!p) break;
            if (p->front) state->ground_front = mugen_expr_eval(p->front, state);
            if (p->back) state->ground_back = mugen_expr_eval(p->back, state);
            break;
        }
        case MUGEN_SC_SUPERPAUSE:
            break;
        case MUGEN_SC_POWERADD:
        {
            Mugen_PowerAdd_Params* p = sc->params;
            if (!p || !p->value) break;
            state->power += mugen_expr_eval(p->value, state);
            if (state->power > state->powermax) state->power = state->powermax;
            if (state->power < 0) state->power = 0;
            break;
        }
        case MUGEN_SC_TARGETBIND:
        {
            Mugen_TargetBind_Params* p = sc->params;
            if (state->target_count == 0) break;
            f32 ox = p && p->pos_x ? mugen_expr_eval(p->pos_x, state) : 0;
            f32 oy = p && p->pos_y ? mugen_expr_eval(p->pos_y, state) : 0;
            for (u32 ti = 0; ti < state->target_count; ti++)
            {
                Mugen_Char_State* t = state->targets[ti].state;
                t->pos_x = state->pos_x + ox * state->facing;
                t->pos_y = state->pos_y + oy;
                t->vel_x = 0;
                t->vel_y = 0;
                t->ghv.isbound = true;
            }
            break;
        }
        case MUGEN_SC_TARGETSTATE:
        {
            Mugen_TargetState_Params* p = sc->params;
            if (!p || !p->value || state->target_count == 0) break;
            i32 sno = (i32)mugen_expr_eval(p->value, state);
            for (u32 ti = 0; ti < state->target_count; ti++)
            {
                Mugen_Char_State* t = state->targets[ti].state;
                t->pending_state = sno;
                t->pending_ctrl = 0;
                t->state_changed = true;
                t->state_owner_cns = state->self_cns;
            }
            break;
        }
        case MUGEN_SC_TARGETLIFEADD:
        {
            Mugen_TargetLifeAdd_Params* p = sc->params;
            if (!p || !p->value || state->target_count == 0) break;
            f32 dmg = mugen_expr_eval(p->value, state);
            bool absolute = p->absolute && mugen_expr_eval(p->absolute, state) != 0.0f;
            bool kill = p->kill ? mugen_expr_eval(p->kill, state) != 0.0f : true;
            for (u32 ti = 0; ti < state->target_count; ti++)
            {
                Mugen_Char_State* t = state->targets[ti].state;
                f32 d = absolute ? dmg : dmg * (t->defence_mul > 0 ? t->defence_mul : 1.0f);
                t->life += d;
                if (t->life > t->lifemax) t->life = t->lifemax;
                if (!kill && t->life < 1) t->life = 1;
                if (t->life < 0) t->life = 0;
            }
            break;
        }
        case MUGEN_SC_TARGETFACING:
        {
            Mugen_TargetFacing_Params* p = sc->params;
            if (!p || !p->value || state->target_count == 0) break;
            i32 val = (i32)mugen_expr_eval(p->value, state);
            for (u32 ti = 0; ti < state->target_count; ti++)
            {
                Mugen_Char_State* t = state->targets[ti].state;
                if (val > 0) t->facing = state->facing;
                else if (val < 0) t->facing = -state->facing;
            }
            break;
        }
        case MUGEN_SC_TARGETPOWERADD:
        {
            Mugen_TargetPowerAdd_Params* p = sc->params;
            if (!p || !p->value || state->target_count == 0) break;
            for (u32 ti = 0; ti < state->target_count; ti++)
            {
                Mugen_Char_State* t = state->targets[ti].state;
                t->power += mugen_expr_eval(p->value, state);
                if (t->power > t->powermax) t->power = t->powermax;
                if (t->power < 0) t->power = 0;
            }
            break;
        }
        case MUGEN_SC_CHANGEANIM2:
        {
            Mugen_ChangeAnim2_Params* p = sc->params;
            if (!p || !p->value) break;
            i32 anim = (i32)mugen_expr_eval(p->value, state);
            state->anim = (u32)anim;
            state->pending_anim = anim;
            state->animelem = 0;
            state->animtime = -999;
            state->animelemtime = 0;
            state->use_owner_anim = true;
            break;
        }
        case MUGEN_SC_HELPER:
        {
            Mugen_Helper_Params* p = sc->params;
            if (!p) break;
            state->helper_spawn_pending = true;
            state->helper_spawn_id = p->id ? (i32)mugen_expr_eval(p->id, state) : 0;
            state->helper_spawn_stateno = p->stateno ? (i32)mugen_expr_eval(p->stateno, state) : 0;
            state->helper_spawn_x = p->pos_x ? mugen_expr_eval(p->pos_x, state) : 0;
            state->helper_spawn_y = p->pos_y ? mugen_expr_eval(p->pos_y, state) : 0;
            state->helper_spawn_postype = p->postype;
            state->helper_spawn_facing = p->facing ? (i32)mugen_expr_eval(p->facing, state) : 1;
            break;
        }
        case MUGEN_SC_DESTROYSELF:
        {
            state->destroy_self_pending = true;
            break;
        }
    }
}

static void tick_controllers(Mugen_Statedef* def, Mugen_Char_State* state)
{
    state->state_changed = false;
    state->pending_state = -1;
    state->pending_ctrl = -1;
    state->pending_anim = -1;

    for (u32 i = 0; i < def->controller_count; i++)
    {
        Mugen_State_Controller* sc = &def->controllers[i];

        if (state->hitpause_time > 0 && !sc->ignorehitpause)
            continue;

        if (sc->persistent == 0)
        {
            if (sc->persistent_counter > 0) continue;
        }
        else if (sc->persistent > 0 && sc->persistent != 1)
        {
            if (sc->persistent_counter > 0)
            {
                sc->persistent_counter--;
                continue;
            }
        }

        if (!eval_triggers(sc, state)) continue;

        exec_controller(sc, state);

        if (sc->persistent == 0)
            sc->persistent_counter = 1;
        else if (sc->persistent > 1)
            sc->persistent_counter = sc->persistent - 1;

        if (state->state_changed) break;
    }
}

void mugen_cns_tick_statedef(Mugen_Statedef* def, Mugen_Char_State* state)
{
    if (!def) return;
    tick_controllers(def, state);
}

void mugen_cns_tick(Mugen_Cns* cns, Mugen_Char_State* state)
{
    Mugen_Statedef* def = mugen_cns_get(cns, state->stateno);
    if (!def) return;

    if (state->nothitby_time > 0)
        state->nothitby_time--;

    tick_controllers(def, state);

    if (state->hitpause_time > 0)
    {
        state->hitpause_time--;
    }
    else if (!state->state_changed)
    {
        if (!state->pos_freeze)
        {
            switch (state->physics)
            {
                case MUGEN_PHYSICS_S:
                    state->vel_x *= state->stand_friction;
                    state->pos_x += state->vel_x * state->facing;
                    break;
                case MUGEN_PHYSICS_C:
                    state->vel_x *= state->crouch_friction;
                    state->pos_x += state->vel_x * state->facing;
                    break;
                case MUGEN_PHYSICS_A:
                    state->vel_y += state->gravity;
                    state->pos_x += state->vel_x * state->facing;
                    state->pos_y += state->vel_y;
                    if (state->pos_y >= 0.0f && state->vel_y > 0.0f)
                    {
                        state->pos_y = 0.0f;
                        state->vel_y = 0.0f;
                        state->state_changed = true;
                        state->pending_state = 52;
                        state->pending_ctrl = 1;
                    }
                    break;
                case MUGEN_PHYSICS_N:
                    state->pos_x += state->vel_x * state->facing;
                    state->pos_y += state->vel_y;
                    break;
                case MUGEN_PHYSICS_L:
                    state->vel_x *= state->stand_friction;
                    state->pos_x += state->vel_x * state->facing;
                    break;
            }
        }
        state->pos_freeze = false;
        state->time++;

        if (state->mctime > 0) state->mctime++;
        if (state->movehit > 0) state->movehit++;
        if (state->moveguarded > 0) state->moveguarded++;

        if (state->ghv.hitshaketime > 0) state->ghv.hitshaketime--;
        if (state->ghv.hittime > 0) state->ghv.hittime--;

        if (state->ghv.fallflag)
            state->fall_time++;
        else
            state->fall_time = 0;
    }

    state->gametime++;
}
