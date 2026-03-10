#include "mugen_cns_parse.h"

typedef struct {
    u32 attr_flags;
    Mugen_Expr* time;
} NotHitBy_Params;

typedef struct {
    Mugen_Expr* front;
    Mugen_Expr* back;
} Width_Params;

typedef struct {
    u32 flags;
} AssertSpecial_Params;

typedef struct {
    Mugen_Expr* value;
} DefenceMulSet_Params;

static void nothitby_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(NotHitBy_Params));
    NotHitBy_Params* p = sc->params;
    if (str8_ieq_cstr(key, "value")) p->attr_flags = mcns_parse_attr(val);
    else if (str8_ieq_cstr(key, "time")) p->time = mugen_expr_parse(val, alloc);
}

static void width_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(Width_Params));
    Width_Params* p = sc->params;
    if (str8_ieq_cstr(key, "value"))
    {
        str8 rest;
        str8 front = mcns_split_comma_first(val, &rest);
        p->front = mugen_expr_parse(front, alloc);
        if (rest.len > 0) p->back = mugen_expr_parse(rest, alloc);
    }
}

static void assertspecial_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    (void)alloc;
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(AssertSpecial_Params));
    AssertSpecial_Params* p = sc->params;
    if (str8_ieq_cstr(key, "flag") || str8_ieq_cstr(key, "flag2") || str8_ieq_cstr(key, "flag3"))
    {
        if (str8_ieq_cstr(val, "nowalk"))         p->flags |= MUGEN_ASSERT_NOWALK;
        else if (str8_ieq_cstr(val, "noautoturn")) p->flags |= MUGEN_ASSERT_NOAUTOTURN;
        else if (str8_ieq_cstr(val, "nostandguard")) p->flags |= MUGEN_ASSERT_NOSTANDGUARD;
        else if (str8_ieq_cstr(val, "nocrouchguard")) p->flags |= MUGEN_ASSERT_NOCROUCHGUARD;
        else if (str8_ieq_cstr(val, "noairguard")) p->flags |= MUGEN_ASSERT_NOAIRGUARD;
        else if (str8_ieq_cstr(val, "unguardable")) p->flags |= MUGEN_ASSERT_UNGUARDABLE;
        else if (str8_ieq_cstr(val, "nojugglecheck")) p->flags |= MUGEN_ASSERT_NOJUGGLECHECK;
        else if (str8_ieq_cstr(val, "intro"))         p->flags |= MUGEN_ASSERT_INTRO;
        else if (str8_ieq_cstr(val, "nocornerpush"))  p->flags |= MUGEN_ASSERT_NOCORNERPUSH;
    }
}

static void defencemulset_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(DefenceMulSet_Params));
    DefenceMulSet_Params* p = sc->params;
    if (str8_ieq_cstr(key, "value")) p->value = mugen_expr_parse(val, alloc);
}

static void nothitby_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    NotHitBy_Params* p = sc->params;
    if (!p) return;
    state->nothitby_attr = p->attr_flags;
    state->nothitby_time = p->time ? (i32)mugen_expr_eval(p->time, state) : 1;
}

static void width_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    Width_Params* p = sc->params;
    if (!p) return;
    if (p->front) state->ground_front = mugen_expr_eval(p->front, state);
    if (p->back) state->ground_back = mugen_expr_eval(p->back, state);
}

static void assertspecial_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    AssertSpecial_Params* p = sc->params;
    if (p) state->assert_flags |= p->flags;
}

static void defencemulset_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    DefenceMulSet_Params* p = sc->params;
    if (p && p->value)
        state->defence_mul = mugen_expr_eval(p->value, state);
}

__attribute__((constructor))
static void register_combat(void)
{
    mugen_sc_register(MUGEN_SC_NOTHITBY, "nothitby", nothitby_parse, nothitby_exec);
    mugen_sc_register(MUGEN_SC_WIDTH, "width", width_parse, width_exec);
    mugen_sc_register(MUGEN_SC_ASSERTSPECIAL, "assertspecial", assertspecial_parse, assertspecial_exec);
    mugen_sc_register(MUGEN_SC_DEFENCEMULSET, "defencemulset", defencemulset_parse, defencemulset_exec);
}
