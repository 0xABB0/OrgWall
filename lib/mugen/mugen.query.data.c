#include "mugen.cns.h"
#include "string.str8.h"

static f32 eval_const(Mugen_Expr* arg, Mugen_Char_State* state)
{
    if (!arg || arg->type != MUGEN_EXPR_LIT_STRING) return 0.0f;
    str8 name = arg->lit_string;
    if (str8_ieq_cstr(name, "velocity.walk.fwd.x"))     return state->walk_fwd_x;
    if (str8_ieq_cstr(name, "velocity.walk.back.x"))    return state->walk_back_x;
    if (str8_ieq_cstr(name, "velocity.jump.neu.x"))     return state->jump_neu_x;
    if (str8_ieq_cstr(name, "velocity.jump.fwd.x"))     return state->jump_fwd_x;
    if (str8_ieq_cstr(name, "velocity.jump.back.x"))    return state->jump_back_x;
    if (str8_ieq_cstr(name, "velocity.jump.y"))         return state->jump_y;
    if (str8_ieq_cstr(name, "velocity.runjump.fwd.x"))  return state->runjump_fwd_x;
    if (str8_ieq_cstr(name, "velocity.runjump.back.x")) return state->runjump_back_x;
    if (str8_ieq_cstr(name, "velocity.runjump.y"))      return state->runjump_y;
    if (str8_ieq_cstr(name, "velocity.run.fwd.x"))     return state->run_fwd_x;
    if (str8_ieq_cstr(name, "velocity.run.back.x"))    return state->run_back_x;
    if (str8_ieq_cstr(name, "velocity.run.back.y"))    return state->run_back_y;
    if (str8_ieq_cstr(name, "velocity.airjump.neu.x")) return state->airjump_neu_x;
    if (str8_ieq_cstr(name, "velocity.airjump.fwd.x")) return state->airjump_fwd_x;
    if (str8_ieq_cstr(name, "velocity.airjump.back.x")) return state->airjump_back_x;
    if (str8_ieq_cstr(name, "velocity.airjump.y"))     return state->airjump_y;
    if (str8_ieq_cstr(name, "movement.stand.friction.threshold")) return state->stand_friction_threshold;
    if (str8_ieq_cstr(name, "movement.crouch.friction.threshold")) return state->crouch_friction_threshold;
    if (str8_ieq_cstr(name, "movement.yaccel"))        return state->gravity;
    if (str8_ieq_cstr(name, "data.attack"))             return state->data_attack;
    if (str8_ieq_cstr(name, "size.ground.front"))      return state->ground_front;
    if (str8_ieq_cstr(name, "size.ground.back"))       return state->ground_back;
    if (str8_ieq_cstr(name, "size.height"))            return state->data_height;
    if (str8_ieq_cstr(name, "data.defence"))           return state->data_defence;
    if (str8_ieq_cstr(name, "data.liedown.time"))      return state->data_liedown_time;
    if (str8_ieq_cstr(name, "data.airjuggle"))         return state->data_airjuggle;
    if (str8_ieq_cstr(name, "data.life"))             return state->lifemax;
    if (str8_ieq_cstr(name, "data.power"))            return state->powermax;
    if (str8_ieq_cstr(name, "data.sparkno"))          return state->data_sparkno;
    if (str8_ieq_cstr(name, "data.guard.sparkno"))    return state->data_guard_sparkno;
    if (str8_ieq_cstr(name, "size.xscale"))           return state->data_xscale;
    if (str8_ieq_cstr(name, "size.yscale"))           return state->data_yscale;
    if (str8_ieq_cstr(name, "size.air.front"))        return state->data_air_front;
    if (str8_ieq_cstr(name, "size.air.back"))         return state->data_air_back;
    if (str8_ieq_cstr(name, "movement.airjump.num"))  return state->data_airjump_num;
    if (str8_ieq_cstr(name, "movement.airjump.height")) return state->data_airjump_height;
    if (str8_ieq_cstr(name, "movement.down.bounce.offset.x")) return state->down_bounce_offset_x;
    if (str8_ieq_cstr(name, "movement.down.bounce.offset.y")) return state->down_bounce_offset_y;
    if (str8_ieq_cstr(name, "movement.down.bounce.yaccel"))   return state->down_bounce_yaccel;
    if (str8_ieq_cstr(name, "movement.down.bounce.groundlevel")) return state->down_bounce_groundlevel;
    if (str8_ieq_cstr(name, "movement.down.friction.threshold")) return state->down_friction_threshold;
    return 0.0f;
}

static f32 eval_selfanimexist(Mugen_Expr* arg, Mugen_Char_State* state)
{
    if (!state->anim_exists) return 0.0f;
    i32 anim_id = arg ? (i32)mugen_expr_eval(arg, state) : (i32)state->anim;
    return state->anim_exists(state->anim_exists_ctx, (u32)anim_id) ? 1.0f : 0.0f;
}

__attribute__((constructor))
static void register_data_queries(void)
{
    mugen_query_register(MUGEN_QUERY_CONST,         "const",         eval_const);
    mugen_query_register(MUGEN_QUERY_SELFANIMEXIST, "selfanimexist", eval_selfanimexist);
}
