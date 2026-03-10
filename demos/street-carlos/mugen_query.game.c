#include "mugen_cns.h"
#include "string.str8.h"

static u64 mugen_rng_next(u64* s)
{
    u64 x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *s = x;
    return x;
}

static f32 eval_alive(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return state->alive ? 1.0f : 0.0f;
}

static f32 eval_random(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return (f32)(mugen_rng_next(&state->rng_state) % 1000);
}

static f32 eval_gametime(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return (f32)state->gametime;
}

static f32 eval_roundstate(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return (f32)state->roundstate;
}

static f32 eval_ishelper(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return state->is_helper ? 1.0f : 0.0f;
}

static f32 eval_numhelper(Mugen_Expr* arg, Mugen_Char_State* state)
{
    if (state->query_num_helper)
    {
        i32 id = arg ? (i32)mugen_expr_eval(arg, state) : 0;
        return (f32)state->query_num_helper(state->helper_ctx, id);
    }
    return 0.0f;
}

static f32 eval_numprojid(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg; (void)state;
    return 0.0f;
}

static f32 eval_p2statetype(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return (f32)state->p2_statetype;
}

static f32 eval_p2movetype(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return (f32)state->p2_movetype;
}

static f32 eval_roundno(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return (f32)state->roundno;
}

static f32 eval_roundsexisted(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return (f32)state->roundsexisted;
}

static f32 eval_palno(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return (f32)state->palno;
}

static f32 eval_lose(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return state->lose ? 1.0f : 0.0f;
}

static f32 eval_win(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return state->win ? 1.0f : 0.0f;
}

static f32 eval_matchover(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return state->matchover ? 1.0f : 0.0f;
}

__attribute__((constructor))
static void register_game_queries(void)
{
    mugen_query_register(MUGEN_QUERY_ALIVE,         "alive",         eval_alive);
    mugen_query_register(MUGEN_QUERY_RANDOM,        "random",        eval_random);
    mugen_query_register(MUGEN_QUERY_GAMETIME,      "gametime",      eval_gametime);
    mugen_query_register(MUGEN_QUERY_ROUNDSTATE,    "roundstate",    eval_roundstate);
    mugen_query_register(MUGEN_QUERY_ISHELPER,      "ishelper",      eval_ishelper);
    mugen_query_register(MUGEN_QUERY_NUMHELPER,     "numhelper",     eval_numhelper);
    mugen_query_register(MUGEN_QUERY_NUMPROJID,     "numprojid",     eval_numprojid);
    mugen_query_register(MUGEN_QUERY_P2STATETYPE,   "p2statetype",   eval_p2statetype);
    mugen_query_register(MUGEN_QUERY_P2MOVETYPE,    "p2movetype",    eval_p2movetype);
    mugen_query_register(MUGEN_QUERY_ROUNDNO,       "roundno",       eval_roundno);
    mugen_query_register(MUGEN_QUERY_ROUNDSEXISTED, "roundsexisted", eval_roundsexisted);
    mugen_query_register(MUGEN_QUERY_PALNO,         "palno",         eval_palno);
    mugen_query_register(MUGEN_QUERY_LOSE,          "lose",          eval_lose);
    mugen_query_register(MUGEN_QUERY_WIN,           "win",           eval_win);
    mugen_query_register(MUGEN_QUERY_MATCHOVER,     "matchover",     eval_matchover);
}
