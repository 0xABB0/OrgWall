#include "mugen.fighter.h"
#include "mugen.air.h"
#include "string.str8.h"
#include "allocator.h"
#include <string.h>
#include <assert.h>
#include <stdio.h>
#include <math.h>

static i32 query_num_helper_cb(void* ctx, i32 id)
{
    Fighter* f = ctx;
    i32 count = 0;
    for (u32 i = 0; i < f->helper_count; i++)
    {
        if (f->helpers[i].helper_id == id)
            count++;
    }
    return count;
}

static Mugen_Char_State* query_helper_state_cb(void* ctx, i32 id)
{
    Fighter* f = ctx;
    for (u32 i = 0; i < f->helper_count; i++)
    {
        if (f->helpers[i].helper_id == id)
            return &f->helpers[i].cns_state;
    }
    return NULL;
}

static Mugen_Char_State* query_root_state_cb(void* ctx)
{
    Fighter* f = ctx;
    return &f->cns_state;
}

static void sync_elem_ticks_for(Mugen_Char_State* st, const Mel_Alloc* alloc)
{
    if (!st->anim_action) return;

    u32 fc = st->anim_action->frame_count;

    if (st->anim_elem_start_ticks && st->anim_elem_count != fc)
    {
        mel_dealloc(alloc, st->anim_elem_start_ticks);
        st->anim_elem_start_ticks = NULL;
    }

    if (!st->anim_elem_start_ticks && fc > 0)
        st->anim_elem_start_ticks = mel_alloc(alloc, sizeof(i32) * fc);

    st->anim_elem_count = fc;
    i32 cumulative = 0;
    for (u32 i = 0; i < fc; i++)
    {
        st->anim_elem_start_ticks[i] = cumulative;
        i16 t = st->anim_action->frames[i].time;
        cumulative += (t <= 0) ? 1 : (i32)t;
    }
}

static void sync_animtime_for(Mugen_Char_State* st)
{
    if (!st->anim_action) return;

    i32 total_ticks = st->anim_total_ticks;
    if (total_ticks <= 0) total_ticks = 1;

    bool has_hold_forever = (st->anim_action->frames[st->anim_action->frame_count - 1].time == -1);
    bool is_looping = !has_hold_forever;

    if (st->animtime == -999)
    {
        st->animtime = -total_ticks;
    }
    else
    {
        st->animtime++;
        if (is_looping && st->animtime > 0)
        {
            i32 loop_ticks = total_ticks;
            if (st->anim_action->loop_start != MUGEN_AIR_NO_LOOP)
            {
                i32 loop_start_ticks = 0;
                for (u32 i = 0; i < st->anim_action->loop_start; i++)
                {
                    i16 t = st->anim_action->frames[i].time;
                    loop_start_ticks += (t <= 0) ? 1 : (i32)t;
                }
                loop_ticks = total_ticks - loop_start_ticks;
            }
            if (loop_ticks <= 0) loop_ticks = total_ticks;
            st->animtime = -loop_ticks;
        }
    }

    i32 new_elem = 1;
    if (st->anim_elem_start_ticks && st->anim_elem_count > 0)
    {
        for (u32 i = st->anim_elem_count; i > 0; i--)
        {
            if (st->time >= st->anim_elem_start_ticks[i - 1])
            {
                new_elem = (i32)i;
                break;
            }
        }
    }

    if (new_elem != st->animelem)
    {
        st->animelem = new_elem;
        st->animelemtime = 0;
    }
    else
    {
        st->animelemtime++;
    }
}

static bool resolve_and_enter_state(Mugen_Char_State* st, Mugen_Cns* main_cns, Mugen_Cns* common_cns, i32 stateno)
{
    Mugen_Cns* owner = st->state_owner_cns ? st->state_owner_cns : main_cns;
    Mugen_Statedef* def = mugen_cns_get(owner, stateno);
    if (def)
    {
        mugen_cns_enter_state(owner, st, stateno);
        return true;
    }
    if (owner != main_cns)
    {
        def = mugen_cns_get(main_cns, stateno);
        if (def)
        {
            st->state_owner_cns = NULL;
            mugen_cns_enter_state(main_cns, st, stateno);
            return true;
        }
    }
    if (common_cns)
    {
        def = mugen_cns_get(common_cns, stateno);
        if (def)
        {
            st->state_owner_cns = NULL;
            mugen_cns_enter_state(common_cns, st, stateno);
            return true;
        }
    }
    return false;
}

static void cns_enter_state(Fighter* f, i32 stateno)
{
    if (!resolve_and_enter_state(&f->cns_state, f->cns, f->common_cns, stateno) && stateno != 0)
        cns_enter_state(f, 0);
}

static void resolve_and_tick_state(Mugen_Char_State* st, Mugen_Cns* main_cns, Mugen_Cns* common_cns)
{
    Mugen_Cns* owner = st->state_owner_cns ? st->state_owner_cns : main_cns;
    Mugen_Statedef* def = mugen_cns_get(owner, st->stateno);
    if (def)
    {
        mugen_cns_tick(owner, st);
        return;
    }
    if (owner != main_cns)
    {
        def = mugen_cns_get(main_cns, st->stateno);
        if (def)
        {
            st->state_owner_cns = NULL;
            mugen_cns_tick(main_cns, st);
            return;
        }
    }
    if (common_cns)
        mugen_cns_tick(common_cns, st);
}

static void play_action_from(Fighter* f, Fighter* source, u32 action_number)
{
    if (f->current_action == action_number && source == f) return;

    mugen_state_anim_play(&f->cns_state, source->cns_state.air, action_number);
    f->current_action = action_number;
    sync_elem_ticks_for(&f->cns_state, f->alloc);
}

static void play_action(Fighter* f, u32 action_number)
{
    play_action_from(f, f, action_number);
}

static void sync_cns_anim(Fighter* f)
{
    Mugen_Char_State* st = &f->cns_state;
    if (st->anim == f->last_cns_anim && st->animtime != -999) return;
    f->last_cns_anim = st->anim;
    f->current_action = UINT32_MAX;
    Fighter* source = (st->use_owner_anim && f->opponent) ? f->opponent : f;
    play_action_from(f, source, st->anim);
}

static void helper_play_action(Fighter_Helper* h, u32 action_number)
{
    if (h->current_action == action_number) return;

    mugen_state_anim_play(&h->cns_state, h->cns_state.air, action_number);
    h->current_action = action_number;
    sync_elem_ticks_for(&h->cns_state, h->alloc);
}

static void helper_sync_cns_anim(Fighter_Helper* h)
{
    Mugen_Char_State* st = &h->cns_state;
    if (st->anim == h->last_cns_anim && st->animtime != -999) return;
    h->last_cns_anim = st->anim;
    h->current_action = UINT32_MAX;
    helper_play_action(h, st->anim);
}

static void helper_tick(Fighter* parent, Fighter_Helper* h, f32 dt, f32 stage_left, f32 stage_right)
{
    Mugen_Char_State* st = &h->cns_state;

    if (st->state_changed)
    {
        if (st->pending_ctrl >= 0)
            st->ctrl = st->pending_ctrl != 0;
        resolve_and_enter_state(st, parent->cns, parent->common_cns, st->pending_state);
        helper_sync_cns_anim(h);
    }

    st->state_changed = false;
    st->assert_flags = 0;
    st->stage_left = stage_left;
    st->stage_right = stage_right;
    st->ground_front = h->ground_front;
    st->ground_back = h->ground_back;

    if (st->hitpause_time <= 0)
    {
        mugen_state_anim_tick(st);
        sync_animtime_for(st);
    }

    {
        Mugen_Statedef* def2 = mugen_cns_get(parent->cns, -2);
        if (!def2 && parent->common_cns)
            def2 = mugen_cns_get(parent->common_cns, -2);
        if (def2)
            mugen_cns_tick_statedef(def2, st);
    }

    if (st->hitpause_time <= 0)
    {
        {
            Mugen_Statedef* def3 = mugen_cns_get(parent->cns, -3);
            if (!def3 && parent->common_cns)
                def3 = mugen_cns_get(parent->common_cns, -3);
            if (def3)
                mugen_cns_tick_statedef(def3, st);
        }
    }

    if (!st->state_changed)
    {
        resolve_and_tick_state(st, parent->cns, parent->common_cns);

        if (st->state_changed)
        {
            if (st->pending_ctrl >= 0)
                st->ctrl = st->pending_ctrl != 0;
            resolve_and_enter_state(st, parent->cns, parent->common_cns, st->pending_state);
        }
    }

    h->x = st->pos_x;
    h->y = -st->pos_y;

    bool new_facing = (st->facing > 0);
    h->facing_right = new_facing;

    helper_sync_cns_anim(h);
}

static void wire_helper_callbacks(Fighter* parent, Fighter_Helper* h)
{
    Mugen_Char_State* st = &h->cns_state;
    st->query_num_helper = query_num_helper_cb;
    st->query_helper_state = query_helper_state_cb;
    st->query_root_state = query_root_state_cb;
    st->helper_ctx = parent;
}

static void fighter_spawn_helper(Fighter* f, const Mel_Alloc* alloc)
{
    Mugen_Char_State* st = &f->cns_state;
    assert(st->helper_spawn_pending);

    if (f->helper_count >= f->helper_capacity)
    {
        u32 new_cap = f->helper_capacity < 4 ? 4 : f->helper_capacity * 2;
        Fighter_Helper* new_helpers = mel_alloc(alloc, new_cap * sizeof(Fighter_Helper));
        if (f->helper_count > 0)
            memcpy(new_helpers, f->helpers, f->helper_count * sizeof(Fighter_Helper));
        f->helpers = new_helpers;
        f->helper_capacity = new_cap;
    }

    Fighter_Helper* h = &f->helpers[f->helper_count++];
    memset(h, 0, sizeof(*h));

    h->helper_id = st->helper_spawn_id;
    h->alloc = alloc;
    h->current_action = UINT32_MAX;
    h->ground_front = f->ground_front;
    h->ground_back = f->ground_back;

    Mugen_Char_State* hst = &h->cns_state;
    memset(hst, 0, sizeof(*hst));
    hst->air = st->air;
    hst->is_helper = true;
    hst->alive = true;
    hst->facing = (st->helper_spawn_facing >= 0) ? st->facing : -st->facing;
    hst->ctrl = true;

    Mugen_Char_Constants* c = &f->cns->constants;
    hst->gravity = c->yaccel;
    hst->stand_friction = c->stand_friction;
    hst->crouch_friction = c->crouch_friction;
    hst->stand_friction_threshold = c->stand_friction_threshold;
    hst->crouch_friction_threshold = c->crouch_friction_threshold;
    hst->lifemax = (f32)c->life;
    hst->life = hst->lifemax;
    hst->powermax = (f32)c->power_max;
    hst->data_attack = (f32)c->attack;
    hst->data_height = c->height;
    hst->data_defence = (f32)c->defence;
    hst->data_airjuggle = (f32)c->airjuggle;
    hst->self_cns = f->cns;
    hst->rng_state = 67890 + (u64)f->helper_count;
    hst->defence_mul = 1.0f;
    hst->attack_mul = 1.0f;
    hst->palno = 1;
    hst->roundstate = st->roundstate;
    hst->roundno = st->roundno;
    hst->gametime = st->gametime;

    switch (st->helper_spawn_postype)
    {
        case MUGEN_POSTYPE_P1:
            hst->pos_x = st->pos_x + st->helper_spawn_x * st->facing;
            hst->pos_y = st->pos_y + st->helper_spawn_y;
            break;
        case MUGEN_POSTYPE_LEFT:
            hst->pos_x = st->stage_left + st->helper_spawn_x;
            hst->pos_y = st->helper_spawn_y;
            break;
        case MUGEN_POSTYPE_RIGHT:
            hst->pos_x = st->stage_right - st->helper_spawn_x;
            hst->pos_y = st->helper_spawn_y;
            break;
        case MUGEN_POSTYPE_BACK:
        {
            f32 back_x = (st->facing > 0) ? st->pos_x - st->helper_spawn_x : st->pos_x + st->helper_spawn_x;
            hst->pos_x = back_x;
            hst->pos_y = st->pos_y + st->helper_spawn_y;
            break;
        }
        case MUGEN_POSTYPE_FRONT:
        {
            f32 front_x = st->pos_x + st->helper_spawn_x * st->facing;
            hst->pos_x = front_x;
            hst->pos_y = st->pos_y + st->helper_spawn_y;
            break;
        }
    }

    h->x = hst->pos_x;
    h->y = -hst->pos_y;
    h->facing_right = (hst->facing > 0);
    h->last_cns_anim = UINT32_MAX;

    wire_helper_callbacks(f, h);

    resolve_and_enter_state(hst, f->cns, f->common_cns, st->helper_spawn_stateno);
    helper_sync_cns_anim(h);

    st->helper_spawn_pending = false;

    printf("HELPER SPAWN: id=%d stateno=%d pos=%.1f,%.1f\n",
        h->helper_id, st->helper_spawn_stateno, h->x, h->y);
}

static void fighter_destroy_helpers(Fighter* f)
{
    u32 i = 0;
    while (i < f->helper_count)
    {
        Fighter_Helper* h = &f->helpers[i];
        if (h->pending_destroy || h->cns_state.destroy_self_pending)
        {
            if (h->cns_state.anim_elem_start_ticks)
                mel_dealloc(h->alloc, h->cns_state.anim_elem_start_ticks);

            f->helpers[i] = f->helpers[f->helper_count - 1];
            f->helper_count--;

            if (i < f->helper_count)
                wire_helper_callbacks(f, &f->helpers[i]);
        }
        else
        {
            i++;
        }
    }
}

void fighter_init_opt(Fighter* f, Fighter_Init_Opt opt, const Mel_Alloc* alloc)
{
    memset(f, 0, sizeof(*f));
    f->x = opt.start_x;
    f->facing_right = opt.facing_right;
    f->start_x = opt.start_x;
    f->start_facing_right = opt.facing_right;
    f->ground_front = opt.ground_front > 0 ? opt.ground_front : 16.0f;
    f->ground_back = opt.ground_back > 0 ? opt.ground_back : 16.0f;
    f->current_action = UINT32_MAX;
    f->alloc = alloc;

    f->cns_state.air = opt.air;

    command_list_init(&f->commands, opt.facing_right, alloc);

    play_action(f, 0);
}

void fighter_enable_cns(Fighter* f, Mugen_Cns* cns, Mugen_Cns* common_cns, Mugen_Cns* cmd_cns)
{
    f->cns = cns;
    f->common_cns = common_cns;
    f->cmd_cns = cmd_cns;

    Mugen_Char_Constants* c = &cns->constants;

    Mugen_Char_State* st = &f->cns_state;
    Mugen_Air* saved_air = st->air;
    memset(st, 0, sizeof(*st));
    st->air = saved_air;
    st->pos_x = f->x;
    st->pos_y = -f->y;
    st->facing = f->facing_right ? 1.0f : -1.0f;
    st->ctrl = true;
    st->alive = true;
    st->life = (f32)c->life;
    st->lifemax = (f32)c->life;
    st->powermax = (f32)c->power_max;
    st->commands = &f->commands;

    st->gravity = c->yaccel;
    st->stand_friction = c->stand_friction;
    st->crouch_friction = c->crouch_friction;
    st->stand_friction_threshold = c->stand_friction_threshold;
    st->crouch_friction_threshold = c->crouch_friction_threshold;
    st->walk_fwd_x = c->walk_fwd_x;
    st->walk_back_x = c->walk_back_x;
    st->run_fwd_x = c->run_fwd_x;
    st->run_back_x = c->run_back_x;
    st->run_back_y = -c->run_back_y;
    st->jump_neu_x = c->jump_neu_x;
    st->jump_fwd_x = c->jump_fwd_x;
    st->jump_back_x = c->jump_back_x;
    st->jump_y = -c->jump_y;
    st->runjump_fwd_x = c->runjump_fwd_x;
    st->runjump_back_x = c->runjump_back_x;
    st->runjump_y = -c->runjump_y;
    st->airjump_neu_x = c->airjump_neu_x;
    st->airjump_fwd_x = c->airjump_fwd_x;
    st->airjump_back_x = c->airjump_back_x;
    st->airjump_y = -c->airjump_y;
    st->data_attack = (f32)c->attack;
    st->attack_dist = c->attack_dist;
    st->data_height = c->height;
    st->data_defence = (f32)c->defence;
    st->data_liedown_time = (f32)c->liedown_time;
    st->data_airjuggle = (f32)c->airjuggle;
    st->data_sparkno = (f32)c->sparkno;
    st->data_guard_sparkno = (f32)c->guard_sparkno;
    st->data_xscale = c->xscale;
    st->data_yscale = c->yscale;
    st->data_air_front = c->air_front;
    st->data_air_back = c->air_back;
    st->data_airjump_num = (f32)c->airjump_num;
    st->data_airjump_height = (f32)c->airjump_height;
    st->down_bounce_offset_x = c->down_bounce_offset_x;
    st->down_bounce_offset_y = c->down_bounce_offset_y;
    st->down_bounce_yaccel = c->down_bounce_yaccel;
    st->down_bounce_groundlevel = c->down_bounce_groundlevel;
    st->down_friction_threshold = c->down_friction_threshold;
    st->self_cns = cns;
    st->juggle_points_remaining = c->airjuggle;
    st->rng_state = 12345;
    st->defence_mul = 1.0f;
    st->attack_mul = 1.0f;
    st->palno = 1;

    f->ground_front = c->ground_front;
    f->ground_back = c->ground_back;

    st->roundstate = 0;
    st->roundno = 1;
    st->roundsexisted = 0;
    st->query_num_helper = query_num_helper_cb;
    st->query_helper_state = query_helper_state_cb;
    st->query_root_state = query_root_state_cb;
    st->helper_ctx = f;

    f->last_cns_anim = UINT32_MAX;
    cns_enter_state(f, 5900);
    sync_cns_anim(f);
}

static void run_statedef_minus1(Fighter* f)
{
    Mugen_Char_State* st = &f->cns_state;
    if (!f->cmd_cns) return;

    Mugen_Statedef* def = mugen_cns_get(f->cmd_cns, -1);
    if (!def) return;

    mugen_cns_tick_statedef(def, st);

    if (st->state_changed)
    {
        if (st->pending_ctrl >= 0)
            st->ctrl = st->pending_ctrl != 0;
        cns_enter_state(f, st->pending_state);
    }
}

static void engine_movement(Fighter* f)
{
    Mugen_Char_State* st = &f->cns_state;
    if (!st->ctrl) return;

    bool hold_up = command_list_active(&f->commands, S8("holdup"));
    bool hold_down = command_list_active(&f->commands, S8("holddown"));
    bool hold_fwd = command_list_active(&f->commands, S8("holdfwd"));
    bool hold_back = command_list_active(&f->commands, S8("holdback"));

    if (st->statetype == MUGEN_PHYSICS_S)
    {
        if (hold_up)
        {
            cns_enter_state(f, 40);
            return;
        }
        if (hold_down)
        {
            cns_enter_state(f, 10);
            return;
        }
        if ((hold_fwd || hold_back) && st->stateno == 0 && !(st->assert_flags & MUGEN_ASSERT_NOWALK))
        {
            cns_enter_state(f, 20);
            return;
        }
        if (!hold_fwd && !hold_back && st->stateno == 20)
        {
            cns_enter_state(f, 0);
            return;
        }
    }
    else if (st->statetype == MUGEN_PHYSICS_C)
    {
        if (!hold_down)
        {
            cns_enter_state(f, 12);
            return;
        }
        if (hold_up)
        {
            cns_enter_state(f, 40);
            return;
        }
    }
    else if (st->statetype == MUGEN_PHYSICS_A)
    {
    }
}

void fighter_tick(Fighter* f, f32 dt, f32 stage_left, f32 stage_right)
{
    command_list_step(&f->commands,
        f->input_up, f->input_down, f->input_left, f->input_right,
        f->btn_a, f->btn_b, f->btn_c,
        f->btn_x, f->btn_y, f->btn_z,
        false, false, false, false,
        false, false, 0);

    Mugen_Char_State* st = &f->cns_state;

    if (st->state_changed)
    {
        if (st->pending_ctrl >= 0)
            st->ctrl = st->pending_ctrl != 0;
        cns_enter_state(f, st->pending_state);
        sync_cns_anim(f);
    }

    st->commands = &f->commands;
    st->state_changed = false;
    st->assert_flags = 0;
    st->stage_left = stage_left;
    st->stage_right = stage_right;
    st->ground_front = f->ground_front;
    st->ground_back = f->ground_back;

    if (st->hitpause_time <= 0)
    {
        mugen_state_anim_tick(st);
        sync_animtime_for(st);
    }

    i32 prev_stateno = st->stateno;

    if (st->hitpause_time <= 0)
        engine_movement(f);

    if (st->stateno != prev_stateno)
        sync_cns_anim(f);

    {
        Mugen_Statedef* def3 = mugen_cns_get(f->cns, -3);
        if (!def3 && f->common_cns)
            def3 = mugen_cns_get(f->common_cns, -3);
        if (def3)
            mugen_cns_tick_statedef(def3, st);
    }

    {
        Mugen_Statedef* def2 = mugen_cns_get(f->cns, -2);
        if (!def2 && f->common_cns)
            def2 = mugen_cns_get(f->common_cns, -2);
        if (def2)
            mugen_cns_tick_statedef(def2, st);
    }

    if (st->hitpause_time <= 0)
        run_statedef_minus1(f);

    if (st->stateno == 12)
        printf("DBG12: t=%d animtime=%d state_changed=%d anim=%d last_cns=%d\n",
            st->time, st->animtime, st->state_changed, st->anim, f->last_cns_anim);

    if (!st->state_changed)
    {
        resolve_and_tick_state(st, f->cns, f->common_cns);

        if (st->state_changed)
        {
            if (st->pending_ctrl >= 0)
                st->ctrl = st->pending_ctrl != 0;
            cns_enter_state(f, st->pending_state);
        }
    }

    if (st->stateno == 5110 && st->alive && st->time >= (i32)st->data_liedown_time)
    {
        st->pending_state = 5120;
        st->pending_ctrl = 0;
        st->state_changed = true;
        cns_enter_state(f, 5120);
    }

    if (st->stateno != prev_stateno)
        printf("STATE: %d -> %d (type=%d phys=%d ctrl=%d vel=%.2f,%.2f pos=%.2f,%.2f anim=%d animtime=%d movetype=%d)\n",
            prev_stateno, st->stateno, st->statetype, st->physics, st->ctrl,
            st->vel_x, st->vel_y, st->pos_x, st->pos_y, st->anim, st->animtime, st->movetype);
    if (st->stateno >= 5100 && st->stateno <= 5121)
        printf("  RECOVERY[%d]: t=%d vel=%.2f,%.2f pos=%.2f,%.2f movetype=%d bounce_yaccel=%.3f\n",
            st->stateno, st->time, st->vel_x, st->vel_y, st->pos_x, st->pos_y, st->movetype, st->down_bounce_yaccel);

    if (st->hitpause_time <= 0 && st->cornerpush_vel != 0.0f)
    {
        st->pos_x += st->cornerpush_vel * st->facing;
        st->cornerpush_vel *= 0.7f;
        if (fabsf(st->cornerpush_vel) < 0.1f) st->cornerpush_vel = 0.0f;
    }

    f->vel_x = st->vel_x;
    f->vel_y = -st->vel_y;
    f->x = st->pos_x;
    f->y = -st->pos_y;

    bool new_facing = (st->facing > 0);
    if (f->facing_right != new_facing)
    {
        f->facing_right = new_facing;
        command_list_set_facing(&f->commands, new_facing);
    }

    if (f->y < 0.0f) f->y = 0.0f;
    if (f->x < stage_left + f->ground_back) f->x = stage_left + f->ground_back;
    if (f->x > stage_right - f->ground_front) f->x = stage_right - f->ground_front;

    st->pos_x = f->x;

    sync_cns_anim(f);

    if (st->helper_spawn_pending && f->cns)
        fighter_spawn_helper(f, f->alloc);

    for (u32 i = 0; i < f->helper_count; i++)
        helper_tick(f, &f->helpers[i], dt, stage_left, stage_right);

    fighter_destroy_helpers(f);
}

void fighter_apply_combat_state(Fighter* f)
{
    Mugen_Char_State* st = &f->cns_state;
    if (st->state_changed)
    {
        i32 prev_stateno = st->stateno;
        cns_enter_state(f, st->pending_state);
        st->state_changed = false;

        if (st->stateno != prev_stateno)
            printf("COMBAT STATE: %d -> %d (type=%d phys=%d ctrl=%d)\n",
                prev_stateno, st->stateno, st->statetype, st->physics, st->ctrl);

        sync_cns_anim(f);

        bool new_facing = (st->facing > 0);
        if (f->facing_right != new_facing)
        {
            f->facing_right = new_facing;
            command_list_set_facing(&f->commands, new_facing);
        }
    }

    for (u32 i = 0; i < f->helper_count; i++)
    {
        Fighter_Helper* h = &f->helpers[i];
        Mugen_Char_State* hst = &h->cns_state;
        if (hst->state_changed)
        {
            if (hst->pending_ctrl >= 0)
                hst->ctrl = hst->pending_ctrl != 0;
            resolve_and_enter_state(hst, f->cns, f->common_cns, hst->pending_state);
            hst->state_changed = false;
            helper_sync_cns_anim(h);
        }
    }
}

static Fighter_Box clsn_to_world_box(const Mugen_Clsn_Box* boxes, u32 count,
                                     f32 px, f32 py, bool facing_right)
{
    Mugen_Clsn_Box bb = mugen_clsn_bounding_box(boxes, count);
    f32 bx = (f32)bb.x1;
    f32 by = (f32)bb.y1;
    f32 bw = (f32)(bb.x2 - bb.x1);
    f32 bh = (f32)(bb.y2 - bb.y1);

    f32 world_x;
    if (facing_right)
        world_x = px + bx;
    else
        world_x = px - bx - bw;

    return (Fighter_Box){ world_x, py - by - bh, bw, bh };
}

Fighter_Box fighter_hurtbox(Fighter* f)
{
    Mugen_Air_Frame* frame = mugen_state_anim_frame(&f->cns_state);
    if (frame && frame->clsn2_count > 0)
        return clsn_to_world_box(frame->clsn2, frame->clsn2_count, f->x, f->y, f->facing_right);

    f32 w = f->ground_front + f->ground_back;
    f32 h = f->cns ? f->cns->constants.height : 60.0f;
    return (Fighter_Box){ f->x - f->ground_back, f->y, w, h };
}

Fighter_Box fighter_hitbox(Fighter* f)
{
    Mugen_Air_Frame* frame = mugen_state_anim_frame(&f->cns_state);
    if (!frame || frame->clsn1_count == 0)
        return (Fighter_Box){0};

    return clsn_to_world_box(frame->clsn1, frame->clsn1_count, f->x, f->y, f->facing_right);
}

bool fighter_has_active_hitbox(Fighter* f)
{
    Mugen_Air_Frame* frame = mugen_state_anim_frame(&f->cns_state);
    return frame && frame->clsn1_count > 0;
}

Fighter_Box helper_hurtbox(Fighter_Helper* h)
{
    Mugen_Air_Frame* frame = mugen_state_anim_frame(&h->cns_state);
    if (frame && frame->clsn2_count > 0)
        return clsn_to_world_box(frame->clsn2, frame->clsn2_count, h->x, h->y, h->facing_right);

    f32 w = h->ground_front + h->ground_back;
    return (Fighter_Box){ h->x - h->ground_back, h->y, w, 60.0f };
}

Fighter_Box helper_hitbox(Fighter_Helper* h)
{
    Mugen_Air_Frame* frame = mugen_state_anim_frame(&h->cns_state);
    if (!frame || frame->clsn1_count == 0)
        return (Fighter_Box){0};

    return clsn_to_world_box(frame->clsn1, frame->clsn1_count, h->x, h->y, h->facing_right);
}

bool helper_has_active_hitbox(Fighter_Helper* h)
{
    Mugen_Air_Frame* frame = mugen_state_anim_frame(&h->cns_state);
    return frame && frame->clsn1_count > 0;
}

void fighter_round_reset(Fighter* f)
{
    for (u32 i = 0; i < f->helper_count; i++)
    {
        if (f->helpers[i].cns_state.anim_elem_start_ticks)
            mel_dealloc(f->helpers[i].alloc, f->helpers[i].cns_state.anim_elem_start_ticks);
    }
    f->helper_count = 0;

    f->x = f->start_x;
    f->y = 0;
    f->vel_x = 0;
    f->vel_y = 0;
    f->facing_right = f->start_facing_right;

    f->input_left = f->input_right = f->input_up = f->input_down = false;
    f->btn_a = f->btn_b = f->btn_c = false;
    f->btn_x = f->btn_y = f->btn_z = false;

    Mugen_Char_State* st = &f->cns_state;
    st->life = st->lifemax;
    st->alive = true;
    st->ctrl = true;
    st->vel_x = 0;
    st->vel_y = 0;
    st->pos_x = f->start_x;
    st->pos_y = 0;
    st->facing = f->start_facing_right ? 1.0f : -1.0f;
    st->movetype = MUGEN_MOVETYPE_I;
    st->hitdef_active = false;
    st->hitdef_pending = false;
    st->hitcount = 0;
    st->mctime = 0;
    st->movehit = 0;
    st->moveguarded = 0;
    st->roundstate = 0;
    st->roundsexisted++;
    st->assert_flags = 0;
    st->state_changed = false;
    st->helper_spawn_pending = false;
    st->destroy_self_pending = false;
    st->cornerpush_vel = 0.0f;
    st->fall_time = 0;
    st->win = false;
    st->lose = false;
    st->matchover = false;
    mugen_targets_clear(st);

    memset(&st->ghv, 0, sizeof(st->ghv));

    command_list_set_facing(&f->commands, f->facing_right);

    f->last_cns_anim = UINT32_MAX;
    cns_enter_state(f, 5900);
    sync_cns_anim(f);
}

void fighter_shutdown(Fighter* f)
{
    for (u32 i = 0; i < f->helper_count; i++)
    {
        if (f->helpers[i].cns_state.anim_elem_start_ticks)
            mel_dealloc(f->helpers[i].alloc, f->helpers[i].cns_state.anim_elem_start_ticks);
        mugen_targets_free(&f->helpers[i].cns_state);
        mugen_afterimage_free(&f->helpers[i].cns_state);
    }
    f->helper_count = 0;
    mugen_targets_free(&f->cns_state);
    mugen_afterimage_free(&f->cns_state);
}
