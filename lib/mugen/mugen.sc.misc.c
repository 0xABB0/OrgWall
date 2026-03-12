#include "mugen.cns.parse.h"

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
    Mugen_Expr* length;
    Mugen_Expr* timegap;
    Mugen_Expr* framegap;
    u8 trans;
    Mugen_Expr* palbright[3];
    Mugen_Expr* palcontrast[3];
    Mugen_Expr* palpostbright[3];
    Mugen_Expr* paladd[3];
    Mugen_Expr* palmul[3];
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

static void mcns_parse_3i(Mugen_Expr** out, str8 val, const Mel_Alloc* alloc)
{
    str8 rest = val;
    for (i32 i = 0; i < 3; i++)
    {
        str8 part = mcns_split_comma_first(rest, &rest);
        if (part.len > 0) out[i] = mugen_expr_parse(part, alloc);
    }
}

static void mcns_parse_3f(Mugen_Expr** out, str8 val, const Mel_Alloc* alloc)
{
    mcns_parse_3i(out, val, alloc);
}

static void afterimage_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(AfterImage_Params));
    AfterImage_Params* p = sc->params;
    if (str8_ieq_cstr(key, "time")) p->time = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "length")) p->length = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "timegap")) p->timegap = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "framegap")) p->framegap = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "trans"))
    {
        str8 t = mcns_trim(val);
        if (str8_ieq_cstr(t, "add"))           p->trans = MUGEN_TRANS_ADD;
        else if (str8_ieq_cstr(t, "add1"))      p->trans = MUGEN_TRANS_ADD1;
        else if (str8_ieq_cstr(t, "sub"))       p->trans = MUGEN_TRANS_SUB;
        else if (str8_ieq_cstr(t, "addalpha"))  p->trans = MUGEN_TRANS_ADDALPHA;
        else                                     p->trans = MUGEN_TRANS_NONE;
    }
    else if (str8_ieq_cstr(key, "palbright")) mcns_parse_3i(p->palbright, val, alloc);
    else if (str8_ieq_cstr(key, "palcontrast")) mcns_parse_3i(p->palcontrast, val, alloc);
    else if (str8_ieq_cstr(key, "palpostbright")) mcns_parse_3i(p->palpostbright, val, alloc);
    else if (str8_ieq_cstr(key, "paladd")) mcns_parse_3i(p->paladd, val, alloc);
    else if (str8_ieq_cstr(key, "palmul")) mcns_parse_3f(p->palmul, val, alloc);
}

static void afterimage_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    AfterImage_Params* p = sc->params;
    if (!p) return;

    state->afterimage.time = p->time ? (i32)mugen_expr_eval(p->time, state) : -1;
    state->afterimage.length = p->length ? (i32)mugen_expr_eval(p->length, state) : 20;
    state->afterimage.timegap = p->timegap ? (i32)mugen_expr_eval(p->timegap, state) : 1;
    state->afterimage.framegap = p->framegap ? (i32)mugen_expr_eval(p->framegap, state) : 4;
    state->afterimage.trans = p->trans;
    state->afterimage.record_counter = 0;
    state->afterimage.frame_count = 0;
    state->afterimage.head = 0;

    for (i32 i = 0; i < 3; i++)
    {
        state->afterimage.palbright[i] = p->palbright[i] ? (i32)mugen_expr_eval(p->palbright[i], state) : 30;
        state->afterimage.palcontrast[i] = p->palcontrast[i] ? (i32)mugen_expr_eval(p->palcontrast[i], state) : 120;
        state->afterimage.palpostbright[i] = p->palpostbright[i] ? (i32)mugen_expr_eval(p->palpostbright[i], state) : 0;
        state->afterimage.paladd[i] = p->paladd[i] ? (i32)mugen_expr_eval(p->paladd[i], state) : 10;
        state->afterimage.palmul[i] = p->palmul[i] ? mugen_expr_eval(p->palmul[i], state) : 0.65f;
    }
}

static void afterimagetime_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(Value_Params));
    Value_Params* vp = sc->params;
    if (str8_ieq_cstr(key, "time") || str8_ieq_cstr(key, "value")) vp->value = mugen_expr_parse(val, alloc);
}

static void afterimagetime_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    Value_Params* vp = sc->params;
    if (!vp || !vp->value) return;
    state->afterimage.time = (i32)mugen_expr_eval(vp->value, state);
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

static void lifeadd_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    Value_Params* p = sc->params;
    if (!p || !p->value) return;
    state->life += mugen_expr_eval(p->value, state);
    if (state->life > state->lifemax) state->life = state->lifemax;
    if (state->life < 0) state->life = 0;
}

static void powerset_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    Value_Params* p = sc->params;
    if (!p || !p->value) return;
    state->power = mugen_expr_eval(p->value, state);
    if (state->power > state->powermax) state->power = state->powermax;
    if (state->power < 0) state->power = 0;
}

static void movehitreset_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    (void)sc;
    state->mctime = 0;
    state->movehit = 0;
    state->moveguarded = 0;
}

static void hitadd_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    Value_Params* p = sc->params;
    if (!p || !p->value) return;
    state->hitcount += (i32)mugen_expr_eval(p->value, state);
}

static void playerpush_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    Value_Params* p = sc->params;
    if (!p || !p->value) return;
    state->playerpush = mugen_expr_eval(p->value, state) != 0.0f;
}

static void attackdist_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    Value_Params* p = sc->params;
    if (!p || !p->value) return;
    state->attack_dist_override = mugen_expr_eval(p->value, state);
}

static void angleset_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    Value_Params* p = sc->params;
    state->angle = (p && p->value) ? mugen_expr_eval(p->value, state) : 0.0f;
}

static void angleadd_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    Value_Params* p = sc->params;
    if (!p || !p->value) return;
    state->angle += mugen_expr_eval(p->value, state);
}

static void anglemul_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    Value_Params* p = sc->params;
    if (!p || !p->value) return;
    state->angle *= mugen_expr_eval(p->value, state);
}

typedef struct {
    Mugen_Expr* value;
    Mugen_Expr* scale_x;
    Mugen_Expr* scale_y;
} AngleDraw_Params;

static void angledraw_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(AngleDraw_Params));
    AngleDraw_Params* p = sc->params;
    if (str8_ieq_cstr(key, "value")) p->value = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "scale"))
    {
        str8 rest;
        str8 x_str = mcns_split_comma_first(val, &rest);
        p->scale_x = mugen_expr_parse(x_str, alloc);
        if (rest.len > 0) p->scale_y = mugen_expr_parse(rest, alloc);
    }
}

static void angledraw_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    AngleDraw_Params* p = sc->params;
    state->angle_draw = true;
    if (p)
    {
        state->angle_draw_xscale = p->scale_x ? mugen_expr_eval(p->scale_x, state) : 1.0f;
        state->angle_draw_yscale = p->scale_y ? mugen_expr_eval(p->scale_y, state) : 1.0f;
    }
    else
    {
        state->angle_draw_xscale = 1.0f;
        state->angle_draw_yscale = 1.0f;
    }
}

typedef struct {
    u8 trans_type;
    Mugen_Expr* alpha_src;
    Mugen_Expr* alpha_dst;
} Trans_Params;

static void trans_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(Trans_Params));
    Trans_Params* p = sc->params;
    if (str8_ieq_cstr(key, "trans"))
    {
        str8 t = mcns_trim(val);
        if (str8_ieq_cstr(t, "add"))           p->trans_type = MUGEN_TRANS_ADD;
        else if (str8_ieq_cstr(t, "add1"))      p->trans_type = MUGEN_TRANS_ADD1;
        else if (str8_ieq_cstr(t, "sub"))       p->trans_type = MUGEN_TRANS_SUB;
        else if (str8_ieq_cstr(t, "addalpha"))  p->trans_type = MUGEN_TRANS_ADDALPHA;
        else                                     p->trans_type = MUGEN_TRANS_NONE;
    }
    else if (str8_ieq_cstr(key, "alpha"))
    {
        str8 rest;
        str8 src_str = mcns_split_comma_first(val, &rest);
        p->alpha_src = mugen_expr_parse(src_str, alloc);
        if (rest.len > 0) p->alpha_dst = mugen_expr_parse(rest, alloc);
    }
}

static void trans_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    Trans_Params* p = sc->params;
    if (!p) return;
    state->trans_type = p->trans_type;
    if (p->trans_type == MUGEN_TRANS_ADDALPHA)
    {
        state->trans_alpha_src = p->alpha_src ? (i32)mugen_expr_eval(p->alpha_src, state) : 256;
        state->trans_alpha_dst = p->alpha_dst ? (i32)mugen_expr_eval(p->alpha_dst, state) : 0;
    }
    else if (p->trans_type == MUGEN_TRANS_ADD)
    {
        state->trans_alpha_src = 256;
        state->trans_alpha_dst = 256;
    }
    else if (p->trans_type == MUGEN_TRANS_ADD1)
    {
        state->trans_alpha_src = 256;
        state->trans_alpha_dst = 128;
    }
    else if (p->trans_type == MUGEN_TRANS_SUB)
    {
        state->trans_alpha_src = 256;
        state->trans_alpha_dst = 256;
    }
    else
    {
        state->trans_alpha_src = 256;
        state->trans_alpha_dst = 0;
    }
}

typedef struct {
    Mugen_Expr* xvel;
    Mugen_Expr* yvel;
    Mugen_Expr* damage;
    Mugen_Expr* hittime;
    Mugen_Expr* slidetime;
    Mugen_Expr* ctrltime;
    Mugen_Expr* xoff;
    Mugen_Expr* yoff;
    Mugen_Expr* fall_damage;
    Mugen_Expr* fall_xvel;
    Mugen_Expr* fall_yvel;
    Mugen_Expr* fall_recover;
    Mugen_Expr* fall_recovertime;
} GetHitVarSet_Params;

static void gethitvarset_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(GetHitVarSet_Params));
    GetHitVarSet_Params* p = sc->params;
    if (str8_ieq_cstr(key, "xvel")) p->xvel = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "yvel")) p->yvel = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "damage")) p->damage = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "hittime")) p->hittime = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "slidetime")) p->slidetime = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "ctrltime")) p->ctrltime = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "xoff")) p->xoff = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "yoff")) p->yoff = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "fall.damage")) p->fall_damage = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "fall.xvel")) p->fall_xvel = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "fall.yvel")) p->fall_yvel = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "fall.recover")) p->fall_recover = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "fall.recovertime")) p->fall_recovertime = mugen_expr_parse(val, alloc);
}

static void gethitvarset_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    GetHitVarSet_Params* p = sc->params;
    if (!p) return;
    if (p->xvel) state->ghv.xvel = mugen_expr_eval(p->xvel, state);
    if (p->yvel) state->ghv.yvel = mugen_expr_eval(p->yvel, state);
    if (p->damage) state->ghv.damage = (i32)mugen_expr_eval(p->damage, state);
    if (p->hittime) state->ghv.hittime = (i32)mugen_expr_eval(p->hittime, state);
    if (p->slidetime) state->ghv.slidetime = (i32)mugen_expr_eval(p->slidetime, state);
    if (p->ctrltime) state->ghv.ctrltime = (i32)mugen_expr_eval(p->ctrltime, state);
    if (p->xoff) state->ghv.xoff = mugen_expr_eval(p->xoff, state);
    if (p->yoff) state->ghv.yoff = mugen_expr_eval(p->yoff, state);
    if (p->fall_damage) state->ghv.fall_damage = (i32)mugen_expr_eval(p->fall_damage, state);
    if (p->fall_xvel) state->ghv.fall_xvel = mugen_expr_eval(p->fall_xvel, state);
    if (p->fall_yvel) state->ghv.fall_yvel = mugen_expr_eval(p->fall_yvel, state);
    if (p->fall_recover) state->ghv.fall_recover = mugen_expr_eval(p->fall_recover, state) != 0.0f;
    if (p->fall_recovertime) state->ghv.fall_recovertime = (i32)mugen_expr_eval(p->fall_recovertime, state);
}

typedef struct {
    Mugen_Expr* time;
    Mugen_Expr* pos_x;
    Mugen_Expr* pos_y;
} Bind_Params;

static void bind_parse(Mugen_State_Controller* sc, str8 key, str8 val, const Mel_Alloc* alloc)
{
    if (!sc->params) sc->params = mcns_alloc_params(alloc, sizeof(Bind_Params));
    Bind_Params* p = sc->params;
    if (str8_ieq_cstr(key, "time")) p->time = mugen_expr_parse(val, alloc);
    else if (str8_ieq_cstr(key, "pos"))
    {
        str8 rest;
        str8 x_str = mcns_split_comma_first(val, &rest);
        p->pos_x = mugen_expr_parse(x_str, alloc);
        if (rest.len > 0) p->pos_y = mugen_expr_parse(rest, alloc);
    }
}

static void bindtoroot_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    Bind_Params* p = sc->params;
    if (!state->query_root_state || !state->helper_ctx) return;
    Mugen_Char_State* root = state->query_root_state(state->helper_ctx);
    if (!root) return;
    f32 off_x = p && p->pos_x ? mugen_expr_eval(p->pos_x, state) : 0.0f;
    f32 off_y = p && p->pos_y ? mugen_expr_eval(p->pos_y, state) : 0.0f;
    state->pos_x = root->pos_x + off_x * root->facing;
    state->pos_y = root->pos_y + off_y;
    state->bound_to = root;
}

static void bindtoparent_exec(Mugen_State_Controller* sc, Mugen_Char_State* state)
{
    Bind_Params* p = sc->params;
    if (!state->bound_to) return;
    Mugen_Char_State* parent = state->bound_to;
    f32 off_x = p && p->pos_x ? mugen_expr_eval(p->pos_x, state) : 0.0f;
    f32 off_y = p && p->pos_y ? mugen_expr_eval(p->pos_y, state) : 0.0f;
    state->pos_x = parent->pos_x + off_x * parent->facing;
    state->pos_y = parent->pos_y + off_y;
}

__attribute__((constructor))
static void register_misc(void)
{
    mugen_sc_register(MUGEN_SC_NULL, "null", NULL, noop_exec);
    mugen_sc_register(MUGEN_SC_PLAYSND, "playsnd", playsnd_parse, noop_exec);
    mugen_sc_register(MUGEN_SC_SUPERPAUSE, "superpause", superpause_parse, noop_exec);
    mugen_sc_register(MUGEN_SC_AFTERIMAGE, "afterimage", afterimage_parse, afterimage_exec);
    mugen_sc_register(MUGEN_SC_AFTERIMAGETIME, "afterimagetime", afterimagetime_parse, afterimagetime_exec);
    mugen_sc_register(MUGEN_SC_FALLENVSHAKE, "fallenvshake", NULL, noop_exec);
    mugen_sc_register(MUGEN_SC_TURN, "turn", NULL, turn_exec);
    mugen_sc_register(MUGEN_SC_SPRPRIORITY, "sprpriority", value_parse, sprpriority_exec);
    mugen_sc_register(MUGEN_SC_POWERADD, "poweradd", value_parse, poweradd_exec);
    mugen_sc_register(MUGEN_SC_LIFESET, "lifeset", value_parse, lifeset_exec);
    mugen_sc_register(MUGEN_SC_LIFEADD, "lifeadd", value_parse, lifeadd_exec);
    mugen_sc_register(MUGEN_SC_POWERSET, "powerset", value_parse, powerset_exec);
    mugen_sc_register(MUGEN_SC_MOVEHITRESET, "movehitreset", NULL, movehitreset_exec);
    mugen_sc_register(MUGEN_SC_HITADD, "hitadd", value_parse, hitadd_exec);
    mugen_sc_register(MUGEN_SC_PLAYERPUSH, "playerpush", value_parse, playerpush_exec);
    mugen_sc_register(MUGEN_SC_ATTACKDIST, "attackdist", value_parse, attackdist_exec);
    mugen_sc_register(MUGEN_SC_ANGLESET, "angleset", value_parse, angleset_exec);
    mugen_sc_register(MUGEN_SC_ANGLEADD, "angleadd", value_parse, angleadd_exec);
    mugen_sc_register(MUGEN_SC_ANGLEMUL, "anglemul", value_parse, anglemul_exec);
    mugen_sc_register(MUGEN_SC_ANGLEDRAW, "angledraw", angledraw_parse, angledraw_exec);
    mugen_sc_register(MUGEN_SC_TRANS, "trans", trans_parse, trans_exec);
    mugen_sc_register(MUGEN_SC_GETHITVARSET, "gethitvarset", gethitvarset_parse, gethitvarset_exec);
    mugen_sc_register(MUGEN_SC_BINDTOROOT, "bindtoroot", bind_parse, bindtoroot_exec);
    mugen_sc_register(MUGEN_SC_BINDTOPARENT, "bindtoparent", bind_parse, bindtoparent_exec);
}
