#include "mugen_cns_parse.h"

typedef struct {
    Mugen_Expr* group;
    Mugen_Expr* index;
    Mugen_Expr* channel;
} PlaySnd_Params;

typedef struct {
    Mugen_Expr* time;
    Mugen_Expr* anim;
    Mugen_Expr* sound_group;
    Mugen_Expr* sound_index;
    Mugen_Expr* pos_x;
    Mugen_Expr* pos_y;
    Mugen_Expr* poweradd;
} SuperPause_Params;

typedef struct {
    Mugen_Expr* time;
} AfterImage_Params;

typedef struct {
    Mugen_Expr* value;
} Value_Params;

static void playsnd_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(PlaySnd_Params));
    PlaySnd_Params* p = sc->params;
    if (str8_ieq_cstr(key, "value"))
    {
        str8 rest;
        str8 grp = mcns_split_comma_first(val, &rest);
        p->group = mugen_expr_parse(grp, alloc);
        if (rest.len > 0) p->index = mugen_expr_parse(rest, alloc);
    }
    else if (str8_ieq_cstr(key, "channel")) p->channel = mugen_expr_parse(val, alloc);
}

static void superpause_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(SuperPause_Params));
    SuperPause_Params* p = sc->params;
    if (str8_ieq_cstr(key, "time")) p->time = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "anim")) p->anim = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "sound"))
    {
        str8 rest;
        str8 grp = mcns_split_comma_first(val, &rest);
        p->sound_group = mugen_expr_parse(grp, alloc);
        if (rest.len > 0) p->sound_index = mugen_expr_parse(rest, alloc);
    }
    else if (str8_ieq_cstr(key, "pos"))
    {
        str8 rest;
        str8 px = mcns_split_comma_first(val, &rest);
        p->pos_x = mugen_expr_parse(px, alloc);
        if (rest.len > 0) p->pos_y = mugen_expr_parse(rest, alloc);
    }
    else if (str8_ieq_cstr(key, "poweradd")) p->poweradd = mugen_expr_parse(val, alloc);
}

static void afterimage_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(AfterImage_Params));
    AfterImage_Params* p = sc->params;
    if (str8_ieq_cstr(key, "time")) p->time = mugen_expr_parse(val, alloc);
}

static void value_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(Value_Params));
    Value_Params* p = sc->params;
    if (str8_ieq_cstr(key, "value")) p->value = mugen_expr_parse(val, alloc);
}

static void noop_exec(Mugen_State_Controller* sc, Mugen_Char_State* state) { (void)sc; (void)state; }

static void turn_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    (void)sc;
    state->facing = -state->facing;
}

static void sprpriority_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    Value_Params* p = sc->params;
    if (!p || !p->value) return;
    state->sprpriority = (i32)mugen_expr_eval(p->value, state);
}

static void poweradd_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    Value_Params* p = sc->params;
    if (!p || !p->value) return;
    state->power += mugen_expr_eval(p->value, state);
    if (state->power > state->powermax) state->power = state->powermax;
    if (state->power < 0) state->power = 0;
}

static void lifeset_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    Value_Params* p = sc->params;
    if (!p || !p->value) return;
    state->life = mugen_expr_eval(p->value, state);
    if (state->life > state->lifemax) state->life = state->lifemax;
    if (state->life < 0) state->life = 0;
}

__attribute__((constructor))
static void register_misc(void)
{
    mugen_sc_register(MUGEN_SC_NULL, "null", NULL, noop_exec);
    mugen_sc_register(MUGEN_SC_PLAYSND, "playsnd", playsnd_parse, noop_exec);
    mugen_sc_register(MUGEN_SC_SUPERPAUSE, "superpause", superpause_parse, noop_exec);
    mugen_sc_register(MUGEN_SC_AFTERIMAGE, "afterimage", afterimage_parse, noop_exec);
    mugen_sc_register(MUGEN_SC_AFTERIMAGETIME, "afterimagetime", afterimage_parse, noop_exec);
    mugen_sc_register(MUGEN_SC_FALLENVSHAKE, "fallenvshake", NULL, noop_exec);
    mugen_sc_register(MUGEN_SC_TURN, "turn", NULL, turn_exec);
    mugen_sc_register(MUGEN_SC_SPRPRIORITY, "sprpriority", value_parse, sprpriority_exec);
    mugen_sc_register(MUGEN_SC_POWERADD, "poweradd", value_parse, poweradd_exec);
    mugen_sc_register(MUGEN_SC_LIFESET, "lifeset", value_parse, lifeset_exec);
}
