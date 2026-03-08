#include "mugen_cns.h"
#include "string.str8.h"
#include <string.h>
#include <stdlib.h>

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

void mugen_cns_enter_state(Mugen_Cns* cns, Mugen_Char_State* state, i32 stateno)
{
    Mugen_Statedef* def = mugen_cns_get(cns, stateno);
    if (!def) return;

    state->prevstateno = state->stateno;
    state->stateno = stateno;
    state->time = 0;
    state->state_changed = false;
    state->hitdef_pending = false;
    state->hitdef_active = false;
    state->movecontact = false;
    state->movehit = false;
    state->moveguarded = false;

    if (def->statetype) state->statetype = def->statetype;
    if (def->movetype)  state->movetype = def->movetype;
    if (def->physics)   state->physics = def->physics;
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

    state->movecontact = false;
    state->movehit = false;
    state->moveguarded = false;

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
        case MUGEN_SC_SPRPRIORITY:
        case MUGEN_SC_FALLENVSHAKE:
            break;

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
            if (p->x) state->pos_x += mugen_expr_eval(p->x, state);
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
            state->vel_y -= state->gravity;
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
            i32 val = (hi > lo) ? lo + (rand() % (hi - lo + 1)) : lo;
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
            r->hitflag = p->hitflag;
            r->guardflag = p->guardflag;
            r->ground_type = p->ground_type;
            r->animtype = p->animtype;
            r->damage_hit = eval_or_default(p->damage_hit, state, 0);
            r->damage_guard = eval_or_default(p->damage_guard, state, 0);
            r->pausetime_p1 = (i32)eval_or_default(p->pausetime_p1, state, 0);
            r->pausetime_p2 = (i32)eval_or_default(p->pausetime_p2, state, 0);
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
            r->guard_velocity = eval_or_default(p->guard_velocity, state, 0);
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
        case MUGEN_SC_DEFENCEMULSET:
            break;
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
            break;
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
            sc->persistent_counter = sc->persistent;

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

    tick_controllers(def, state);

    if (!state->state_changed)
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
                    state->vel_y -= state->gravity;
                    state->pos_x += state->vel_x * state->facing;
                    state->pos_y += state->vel_y;
                    if (state->pos_y <= 0.0f && state->vel_y < 0.0f)
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
            }
        }
        state->pos_freeze = false;
        state->time++;
    }

    if (state->nothitby_time > 0)
        state->nothitby_time--;
}
