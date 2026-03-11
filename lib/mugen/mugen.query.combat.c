#include "mugen.cns.h"
#include "string.str8.h"

static f32 eval_movecontact(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return (f32)(state->mctime > 0 ? state->mctime : 0);
}

static f32 eval_movehit(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return (f32)(state->movehit > 0 ? state->movehit : 0);
}

static f32 eval_moveguarded(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return (f32)(state->moveguarded > 0 ? state->moveguarded : 0);
}

static f32 eval_hitcount(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return (f32)state->hitcount;
}

static f32 eval_life(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return state->life;
}

static f32 eval_lifemax(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return state->lifemax;
}

static f32 eval_power(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return state->power;
}

static f32 eval_powermax(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return state->powermax;
}

static f32 eval_hitshakeover(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return state->ghv.hitshaketime <= 0 ? 1.0f : 0.0f;
}

static f32 eval_hitover(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return state->ghv.hittime <= 0 ? 1.0f : 0.0f;
}

static f32 eval_hitfall(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return state->ghv.fallflag ? 1.0f : 0.0f;
}

static f32 eval_canrecover(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return (state->ghv.fall_recover && state->fall_time >= state->ghv.fall_recovertime) ? 1.0f : 0.0f;
}

static f32 eval_hitdefattr(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg; (void)state;
    return 0.0f;
}

static f32 eval_numtarget(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return (f32)state->target_count;
}

static f32 eval_gethitvar(Mugen_Expr* arg, Mugen_Char_State* state)
{
    if (!arg || arg->type != MUGEN_EXPR_LIT_STRING) return 0.0f;
    str8 name = arg->lit_string;
    Mugen_GetHitVar* ghv = &state->ghv;
    if (str8_ieq_cstr(name, "animtype"))        return (f32)ghv->animtype;
    if (str8_ieq_cstr(name, "air.animtype"))    return (f32)ghv->air_animtype;
    if (str8_ieq_cstr(name, "ground.animtype")) return (f32)ghv->ground_animtype;
    if (str8_ieq_cstr(name, "groundtype"))      return (f32)ghv->groundtype;
    if (str8_ieq_cstr(name, "airtype"))          return (f32)ghv->airtype;
    if (str8_ieq_cstr(name, "damage"))          return (f32)ghv->damage;
    if (str8_ieq_cstr(name, "hitcount"))        return (f32)ghv->hitcount;
    if (str8_ieq_cstr(name, "guardcount"))      return (f32)ghv->guardcount;
    if (str8_ieq_cstr(name, "hitshaketime"))    return (f32)ghv->hitshaketime;
    if (str8_ieq_cstr(name, "hittime"))         return (f32)ghv->hittime;
    if (str8_ieq_cstr(name, "slidetime"))       return (f32)ghv->slidetime;
    if (str8_ieq_cstr(name, "ctrltime"))        return (f32)ghv->ctrltime;
    if (str8_ieq_cstr(name, "xvel"))            return ghv->xvel;
    if (str8_ieq_cstr(name, "yvel"))            return ghv->yvel;
    if (str8_ieq_cstr(name, "xaccel"))          return ghv->xaccel;
    if (str8_ieq_cstr(name, "yaccel"))          return ghv->yaccel;
    if (str8_ieq_cstr(name, "xoff"))            return ghv->xoff;
    if (str8_ieq_cstr(name, "yoff"))            return ghv->yoff;
    if (str8_ieq_cstr(name, "isbound"))         return ghv->isbound ? 1.0f : 0.0f;
    if (str8_ieq_cstr(name, "guarded"))         return ghv->guarded ? 1.0f : 0.0f;
    if (str8_ieq_cstr(name, "fall"))            return ghv->fallflag ? 1.0f : 0.0f;
    if (str8_ieq_cstr(name, "fall.recover"))    return ghv->fall_recover ? 1.0f : 0.0f;
    if (str8_ieq_cstr(name, "fall.recovertime")) return (f32)ghv->fall_recovertime;
    if (str8_ieq_cstr(name, "fall.xvel"))       return ghv->fall_xvel;
    if (str8_ieq_cstr(name, "fall.yvel"))       return ghv->fall_yvel;
    if (str8_ieq_cstr(name, "fall.damage"))     return (f32)ghv->fall_damage;
    if (str8_ieq_cstr(name, "fall.kill"))       return ghv->fall_kill ? 1.0f : 0.0f;
    if (str8_ieq_cstr(name, "attr"))            return (f32)ghv->attr;
    if (str8_ieq_cstr(name, "priority"))        return (f32)ghv->priority;
    if (str8_ieq_cstr(name, "forcestand"))      return ghv->forcestand ? 1.0f : 0.0f;
    return 0.0f;
}

__attribute__((constructor))
static void register_combat_queries(void)
{
    mugen_query_register(MUGEN_QUERY_MOVECONTACT,  "movecontact",  eval_movecontact);
    mugen_query_register(MUGEN_QUERY_MOVEHIT,      "movehit",      eval_movehit);
    mugen_query_register(MUGEN_QUERY_MOVEGUARDED,  "moveguarded",  eval_moveguarded);
    mugen_query_register(MUGEN_QUERY_HITCOUNT,     "hitcount",     eval_hitcount);
    mugen_query_register(MUGEN_QUERY_LIFE,         "life",         eval_life);
    mugen_query_register(MUGEN_QUERY_LIFEMAX,      "lifemax",      eval_lifemax);
    mugen_query_register(MUGEN_QUERY_POWER,        "power",        eval_power);
    mugen_query_register(MUGEN_QUERY_POWERMAX,     "powermax",     eval_powermax);
    mugen_query_register(MUGEN_QUERY_HITSHAKEOVER, "hitshakeover", eval_hitshakeover);
    mugen_query_register(MUGEN_QUERY_HITOVER,      "hitover",      eval_hitover);
    mugen_query_register(MUGEN_QUERY_HITFALL,      "hitfall",      eval_hitfall);
    mugen_query_register(MUGEN_QUERY_CANRECOVER,   "canrecover",   eval_canrecover);
    mugen_query_register(MUGEN_QUERY_HITDEFATTR,   "hitdefattr",   eval_hitdefattr);
    mugen_query_register(MUGEN_QUERY_NUMTARGET,    "numtarget",    eval_numtarget);
    mugen_query_register(MUGEN_QUERY_GETHITVAR,    NULL,           eval_gethitvar);
}
