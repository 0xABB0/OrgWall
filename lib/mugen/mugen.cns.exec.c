#include "mugen.cns.h"
#include "str8.h"
#include <string.h>
#include <stdlib.h>

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

static void exec_controller(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    Mugen_SC_Reg* reg = mugen_sc_get_reg(sc->type);
    if (reg && reg->exec)
        reg->exec(sc, state);
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

    state->prev_statetype = state->statetype;
    state->prev_movetype = state->movetype;

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

        if (state->envshake_time > 0) state->envshake_time--;
        if (state->palfx_time > 0) state->palfx_time--;
        if (state->pause_time > 0) state->pause_time--;
    }

    mugen_afterimage_record(state);
    state->gametime++;
}
