#include "mugen.cns.h"
#include "string.str8.h"

static f32 eval_time(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return (f32)state->time;
}

static f32 eval_animtime(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return (f32)state->animtime;
}

static f32 eval_animelem(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return (f32)state->animelem;
}

static f32 eval_animelemtime(Mugen_Expr* arg, Mugen_Char_State* state)
{
    if (arg)
    {
        i32 elem = (i32)mugen_expr_eval(arg, state);
        i32 idx = elem - 1;
        if (idx >= 0 && idx < (i32)state->anim_elem_count)
            return (f32)(state->time - state->anim_elem_start_ticks[idx]);
        if (idx < 0) return (f32)state->time;
        return -9999.0f;
    }
    return (f32)state->animelemtime;
}

static f32 eval_stateno(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return (f32)state->stateno;
}

static f32 eval_prevstateno(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return (f32)state->prevstateno;
}

static f32 eval_statetype(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return (f32)state->statetype;
}

static f32 eval_movetype(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return (f32)state->movetype;
}

static f32 eval_ctrl(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return state->ctrl ? 1.0f : 0.0f;
}

static f32 eval_anim(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return (f32)state->anim;
}

static f32 eval_command(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg; (void)state;
    return 0.0f;
}

__attribute__((constructor))
static void register_state_queries(void)
{
    mugen_query_register(MUGEN_QUERY_TIME,         "time",         eval_time);
    mugen_query_register(MUGEN_QUERY_TIME,         "statetime",    NULL);
    mugen_query_register(MUGEN_QUERY_ANIMTIME,     "animtime",     eval_animtime);
    mugen_query_register(MUGEN_QUERY_ANIMELEM,     "animelem",     eval_animelem);
    mugen_query_register(MUGEN_QUERY_ANIMELEMTIME, "animelemtime", eval_animelemtime);
    mugen_query_register(MUGEN_QUERY_STATENO,      "stateno",      eval_stateno);
    mugen_query_register(MUGEN_QUERY_PREVSTATENO,  "prevstateno",  eval_prevstateno);
    mugen_query_register(MUGEN_QUERY_STATETYPE,    "statetype",    eval_statetype);
    mugen_query_register(MUGEN_QUERY_MOVETYPE,     "movetype",     eval_movetype);
    mugen_query_register(MUGEN_QUERY_CTRL,         "ctrl",         eval_ctrl);
    mugen_query_register(MUGEN_QUERY_ANIM,         "anim",         eval_anim);
    mugen_query_register(MUGEN_QUERY_COMMAND,      "command",      eval_command);
}
