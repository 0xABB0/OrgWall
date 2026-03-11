#include "mugen.cns.h"
#include "string.str8.h"

static f32 eval_vel_x(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return state->vel_x;
}

static f32 eval_vel_y(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return state->vel_y;
}

static f32 eval_pos_x(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return state->pos_x;
}

static f32 eval_pos_y(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return state->pos_y;
}

static f32 eval_facing(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return state->facing;
}

static f32 eval_p2bodydist_x(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    f32 dist = state->p2_pos_x - state->pos_x;
    if (state->facing < 0) dist = -dist;
    dist -= state->ground_front + state->p2_width;
    if (dist < 0) dist = 0;
    return dist;
}

static f32 eval_p2dist_x(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    f32 dist = state->p2_pos_x - state->pos_x;
    if (state->facing < 0) dist = -dist;
    return dist;
}

static f32 eval_p2dist_y(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    return state->p2_pos_y - state->pos_y;
}

static f32 eval_frontedgedist(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    if (state->facing > 0)
        return state->stage_right - state->pos_x;
    return state->pos_x - state->stage_left;
}

static f32 eval_backedgedist(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    if (state->facing > 0)
        return state->pos_x - state->stage_left;
    return state->stage_right - state->pos_x;
}

static f32 eval_frontedgebodydist(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    if (state->facing > 0)
        return state->stage_right - state->pos_x - state->ground_front;
    return state->pos_x - state->stage_left - state->ground_front;
}

static f32 eval_backedgebodydist(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    if (state->facing > 0)
        return state->pos_x - state->stage_left - state->ground_back;
    return state->stage_right - state->pos_x - state->ground_back;
}

static f32 eval_inguarddist(Mugen_Expr* arg, Mugen_Char_State* state)
{
    (void)arg;
    f32 dist = state->p2_pos_x - state->pos_x;
    if (state->facing < 0) dist = -dist;
    return (dist >= 0 && dist <= state->attack_dist) ? 1.0f : 0.0f;
}

__attribute__((constructor))
static void register_movement_queries(void)
{
    mugen_query_register(MUGEN_QUERY_VEL_X,        "vel x",        eval_vel_x);
    mugen_query_register(MUGEN_QUERY_VEL_Y,        "vel y",        eval_vel_y);
    mugen_query_register(MUGEN_QUERY_POS_X,        "pos x",        eval_pos_x);
    mugen_query_register(MUGEN_QUERY_POS_Y,        "pos y",        eval_pos_y);
    mugen_query_register(MUGEN_QUERY_FACING,       "facing",       eval_facing);
    mugen_query_register(MUGEN_QUERY_P2BODYDIST_X, "p2bodydist x", eval_p2bodydist_x);
    mugen_query_register(MUGEN_QUERY_P2DIST_X,     "p2dist x",     eval_p2dist_x);
    mugen_query_register(MUGEN_QUERY_P2DIST_Y,     "p2dist y",     eval_p2dist_y);
    mugen_query_register(MUGEN_QUERY_FRONTEDGEDIST,     "frontedgedist",     eval_frontedgedist);
    mugen_query_register(MUGEN_QUERY_BACKEDGEDIST,      "backedgedist",      eval_backedgedist);
    mugen_query_register(MUGEN_QUERY_FRONTEDGEBODYDIST, "frontedgebodydist", eval_frontedgebodydist);
    mugen_query_register(MUGEN_QUERY_BACKEDGEBODYDIST,  "backedgebodydist",  eval_backedgebodydist);
    mugen_query_register(MUGEN_QUERY_INGUARDDIST,      "inguarddist",       eval_inguarddist);
}
