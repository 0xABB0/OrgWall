#include "mugen.cns.parse.h"
#include <string.h>

typedef struct {
    Mugen_Expr* time;
    Mugen_Expr* ampl;
    Mugen_Expr* freq;
    Mugen_Expr* phase;
} EnvShake_Params;

typedef struct {
    Mugen_Expr* value;
    Mugen_Expr* movecamera_x;
    Mugen_Expr* movecamera_y;
} ScreenBound_Params;

typedef struct {
    Mugen_Expr* time;
    Mugen_Expr* add[3];
    Mugen_Expr* mul[3];
    Mugen_Expr* sinadd[3];
    Mugen_Expr* sinadd_period;
} PalFX_Params;

typedef struct {
    Mugen_Expr* time;
    Mugen_Expr* endcmdbuftime;
    Mugen_Expr* pausebg;
} Pause_Params;

static void envshake_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(EnvShake_Params));
    EnvShake_Params* p = sc->params;
    if (str8_ieq_cstr(key, "time")) p->time = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "ampl")) p->ampl = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "freq")) p->freq = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "phase")) p->phase = mugen_expr_parse(val, alloc);
}

static void envshake_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    EnvShake_Params* p = sc->params;
    if (!p) return;
    state->envshake_time = p->time ? (i32)mugen_expr_eval(p->time, state) : 1;
    state->envshake_ampl = p->ampl ? mugen_expr_eval(p->ampl, state) : -4.0f;
    state->envshake_freq = p->freq ? mugen_expr_eval(p->freq, state) : 60.0f;
    state->envshake_phase = p->phase ? mugen_expr_eval(p->phase, state) : 0.0f;
}

static void screenbound_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(ScreenBound_Params));
    ScreenBound_Params* p = sc->params;
    if (str8_ieq_cstr(key, "value")) p->value = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "movecamera"))
    {
        str8 rest;
        str8 x_str = mcns_split_comma_first(val, &rest);
        p->movecamera_x = mugen_expr_parse(x_str, alloc);
        if (rest.len > 0) p->movecamera_y = mugen_expr_parse(rest, alloc);
    }
}

static void screenbound_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    ScreenBound_Params* p = sc->params;
    if (!p) return;
    state->screenbound_value = p->value ? mugen_expr_eval(p->value, state) != 0.0f : true;
    state->screenbound_movecamera_x = p->movecamera_x ? mugen_expr_eval(p->movecamera_x, state) != 0.0f : false;
    state->screenbound_movecamera_y = p->movecamera_y ? mugen_expr_eval(p->movecamera_y, state) != 0.0f : false;
}

static void palfx_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(PalFX_Params));
    PalFX_Params* p = sc->params;
    if (str8_ieq_cstr(key, "time")) p->time = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "add"))
    {
        str8 rest = val;
        for (i32 i = 0; i < 3; i++)
        {
            str8 part = mcns_split_comma_first(rest, &rest);
            if (part.len > 0) p->add[i] = mugen_expr_parse(part, alloc);
        }
    }
    else if (str8_ieq_cstr(key, "mul"))
    {
        str8 rest = val;
        for (i32 i = 0; i < 3; i++)
        {
            str8 part = mcns_split_comma_first(rest, &rest);
            if (part.len > 0) p->mul[i] = mugen_expr_parse(part, alloc);
        }
    }
    else if (str8_ieq_cstr(key, "sinadd"))
    {
        str8 rest = val;
        for (i32 i = 0; i < 3; i++)
        {
            str8 part = mcns_split_comma_first(rest, &rest);
            if (part.len > 0) p->sinadd[i] = mugen_expr_parse(part, alloc);
        }
        if (rest.len > 0) p->sinadd_period = mugen_expr_parse(rest, alloc);
    }
}

static void palfx_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    PalFX_Params* p = sc->params;
    if (!p) return;
    state->palfx_time = p->time ? (i32)mugen_expr_eval(p->time, state) : 1;
    for (i32 i = 0; i < 3; i++)
    {
        state->palfx_add[i] = p->add[i] ? (i32)mugen_expr_eval(p->add[i], state) : 0;
        state->palfx_mul[i] = p->mul[i] ? (i32)mugen_expr_eval(p->mul[i], state) : 256;
        state->palfx_sinadd[i] = p->sinadd[i] ? (i32)mugen_expr_eval(p->sinadd[i], state) : 0;
    }
    state->palfx_sinadd_period = p->sinadd_period ? (i32)mugen_expr_eval(p->sinadd_period, state) : 1;
}

static void pause_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(Pause_Params));
    Pause_Params* p = sc->params;
    if (str8_ieq_cstr(key, "time")) p->time = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "endcmdbuftime")) p->endcmdbuftime = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "pausebg")) p->pausebg = mugen_expr_parse(val, alloc);
}

static void pause_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    Pause_Params* p = sc->params;
    if (!p) return;
    state->pause_time = p->time ? (i32)mugen_expr_eval(p->time, state) : 0;
    state->pause_endcmdbuftime = p->endcmdbuftime ? (i32)mugen_expr_eval(p->endcmdbuftime, state) : 0;
    state->pause_bg = p->pausebg ? mugen_expr_eval(p->pausebg, state) != 0.0f : true;
}

__attribute__((constructor))
static void register_effects(void)
{
    mugen_sc_register(MUGEN_SC_ENVSHAKE, "envshake", envshake_parse, envshake_exec);
    mugen_sc_register(MUGEN_SC_SCREENBOUND, "screenbound", screenbound_parse, screenbound_exec);
    mugen_sc_register(MUGEN_SC_PALFX, "palfx", palfx_parse, palfx_exec);
    mugen_sc_register(MUGEN_SC_PAUSE_CTRL, "pause", pause_parse, pause_exec);
}
