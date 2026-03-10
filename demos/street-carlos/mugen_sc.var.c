#include "mugen_cns_parse.h"

typedef struct {
    u8 var_type;
    Mugen_Expr* index;
    Mugen_Expr* value;
} VarSet_Params;

typedef struct {
    u8 var_type;
    Mugen_Expr* index;
    Mugen_Expr* range_low;
    Mugen_Expr* range_high;
} VarRandom_Params;

typedef struct {
    Mugen_Expr* value;
    Mugen_Expr* fvalue;
    Mugen_Expr* first;
    Mugen_Expr* last;
} VarRangeSet_Params;

static void varset_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(VarSet_Params));
    VarSet_Params* p = sc->params;
    if (str8_ieq_cstr(key, "value")) p->value = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "v")) { p->var_type = MUGEN_VAR_INT; p->index = mugen_expr_parse(val, alloc); }
    else if (str8_ieq_cstr(key, "fv")) { p->var_type = MUGEN_VAR_FLOAT; p->index = mugen_expr_parse(val, alloc); }
    else if (mcns_starts_with_i(key, "var("))
    {
        p->var_type = MUGEN_VAR_INT;
        str8 inner = str8_from_parts(key.data + 4, key.len - 5);
        p->index = mugen_expr_parse(inner, alloc);
        p->value = mugen_expr_parse(val, alloc);
    }
    else if (mcns_starts_with_i(key, "fvar("))
    {
        p->var_type = MUGEN_VAR_FLOAT;
        str8 inner = str8_from_parts(key.data + 5, key.len - 6);
        p->index = mugen_expr_parse(inner, alloc);
        p->value = mugen_expr_parse(val, alloc);
    }
    else if (mcns_starts_with_i(key, "sysvar("))
    {
        p->var_type = MUGEN_VAR_SYSINT;
        str8 inner = str8_from_parts(key.data + 7, key.len - 8);
        p->index = mugen_expr_parse(inner, alloc);
        p->value = mugen_expr_parse(val, alloc);
    }
    else if (mcns_starts_with_i(key, "sysfvar("))
    {
        p->var_type = MUGEN_VAR_SYSFLOAT;
        str8 inner = str8_from_parts(key.data + 8, key.len - 9);
        p->index = mugen_expr_parse(inner, alloc);
        p->value = mugen_expr_parse(val, alloc);
    }
}

static void varrandom_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(VarRandom_Params));
    VarRandom_Params* p = sc->params;
    if (str8_ieq_cstr(key, "v")) { p->var_type = MUGEN_VAR_INT; p->index = mugen_expr_parse(val, alloc); }
    else if (str8_ieq_cstr(key, "range"))
    {
        str8 rest;
        str8 lo = mcns_split_comma_first(val, &rest);
        p->range_low = mugen_expr_parse(lo, alloc);
        if (rest.len > 0) p->range_high = mugen_expr_parse(rest, alloc);
    }
}

static void varrangeset_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(VarRangeSet_Params));
    VarRangeSet_Params* p = sc->params;
    if (str8_ieq_cstr(key, "value"))  p->value = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "fvalue")) p->fvalue = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "first"))  p->first = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "last"))   p->last = mugen_expr_parse(val, alloc);
}

static u64 rng_next(u64* s)
{
    u64 x = *s;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *s = x;
    return x;
}

static void apply_var(Mugen_Char_State* state, u8 var_type, i32 idx, f32 val, bool add)
{
    switch (var_type)
    {
        case MUGEN_VAR_INT:
            if (idx >= 0 && idx < 60) { if (add) state->var[idx] += (i32)val; else state->var[idx] = (i32)val; }
            break;
        case MUGEN_VAR_FLOAT:
            if (idx >= 0 && idx < 40) { if (add) state->fvar[idx] += val; else state->fvar[idx] = val; }
            break;
        case MUGEN_VAR_SYSINT:
            if (idx >= 0 && idx < 5) { if (add) state->sysvar[idx] += (i32)val; else state->sysvar[idx] = (i32)val; }
            break;
        case MUGEN_VAR_SYSFLOAT:
            if (idx >= 0 && idx < 5) { if (add) state->sysfvar[idx] += val; else state->sysfvar[idx] = val; }
            break;
    }
}

static void varset_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    VarSet_Params* p = sc->params;
    if (!p || !p->value) return;
    i32 idx = p->index ? (i32)mugen_expr_eval(p->index, state) : 0;
    f32 val = mugen_expr_eval(p->value, state);
    apply_var(state, p->var_type, idx, val, false);
}

static void varadd_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    VarSet_Params* p = sc->params;
    if (!p || !p->value) return;
    i32 idx = p->index ? (i32)mugen_expr_eval(p->index, state) : 0;
    f32 val = mugen_expr_eval(p->value, state);
    apply_var(state, p->var_type, idx, val, true);
}

static void varrandom_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    VarRandom_Params* p = sc->params;
    if (!p) return;
    i32 idx = p->index ? (i32)mugen_expr_eval(p->index, state) : 0;
    i32 lo = p->range_low ? (i32)mugen_expr_eval(p->range_low, state) : 0;
    i32 hi = p->range_high ? (i32)mugen_expr_eval(p->range_high, state) : 999;
    i32 val = (hi > lo) ? lo + (i32)(rng_next(&state->rng_state) % (u64)(hi - lo + 1)) : lo;
    if (p->var_type == MUGEN_VAR_INT && idx >= 0 && idx < 60)
        state->var[idx] = val;
}

static void varrangeset_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    VarRangeSet_Params* p = sc->params;
    if (!p) return;
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
}

__attribute__((constructor))
static void register_var(void)
{
    mugen_sc_register(MUGEN_SC_VARSET, "varset", varset_parse, varset_exec);
    mugen_sc_register(MUGEN_SC_VARADD, "varadd", varset_parse, varadd_exec);
    mugen_sc_register(MUGEN_SC_VARRANDOM, "varrandom", varrandom_parse, varrandom_exec);
    mugen_sc_register(MUGEN_SC_VARRANGESET, "varrangeset", varrangeset_parse, varrangeset_exec);
}
