#include "fighter.h"
#include "actions.h"
#include "anim.player.h"
#include "anim.clip.h"
#include "anim.registry.h"
#include "mugen_air.h"
#include "hash.xxh.h"
#include "string.str8.h"
#include "collection.slotmap.h"
#include <string.h>
#include <stdio.h>

static u64 s_hitbox_prop;
static u64 s_hurtbox_prop;
static u64 s_frame_prop;
static bool s_props_init = false;

static void ensure_props(void)
{
    if (s_props_init) return;
    s_hitbox_prop = mel_xxh3_64("hitbox", 6);
    s_hurtbox_prop = mel_xxh3_64("hurtbox", 7);
    s_frame_prop = mel_xxh3_64("frame", 5);
    s_props_init = true;
}

static bool anim_exists_cb(void* ctx, u32 anim)
{
    Fighter* f = ctx;
    for (u32 i = 0; i < f->action_map_count; i++)
    {
        if (f->action_map[i].action_number == anim)
            return true;
    }
    return false;
}

static Mel_Anim_Clip_Handle find_clip(Fighter* f, u32 action_number)
{
    for (u32 i = 0; i < f->action_map_count; i++)
    {
        if (f->action_map[i].action_number == action_number)
            return f->action_map[i].clip;
    }
    return (Mel_Anim_Clip_Handle){0};
}

static void play_action(Fighter* f, u32 action_number, f32 crossfade)
{
    if (f->current_action == action_number) return;

    Mel_Anim_Clip_Handle clip = find_clip(f, action_number);
    if (!mel_slotmap_alive(f->clip_pool, clip)) return;

    f->current_action = action_number;
    mel_anim_player_play(&f->player, clip, .crossfade = crossfade);
}

static void sample_anim(Fighter* f)
{
    mel_anim_player_get_vec4(&f->player, s_hitbox_prop, f->player.alloc, f->anim_hitbox);
    mel_anim_player_get_vec4(&f->player, s_hurtbox_prop, f->player.alloc, f->anim_hurtbox);
}

void fighter_init_opt(Fighter* f, Fighter_Init_Opt opt, const Mel_Alloc* alloc)
{
    ensure_props();
    memset(f, 0, sizeof(*f));
    f->x = opt.start_x;
    f->facing_right = opt.facing_right;
    f->ground_front = opt.ground_front > 0 ? opt.ground_front : 16.0f;
    f->ground_back = opt.ground_back > 0 ? opt.ground_back : 16.0f;
    f->current_action = UINT32_MAX;

    f->clip_pool = opt.clip_pool;
    f->action_map = opt.action_map;
    f->action_map_count = opt.action_map_count;

    mel_anim_player_init(&f->player, alloc, f->clip_pool);

    command_list_init(&f->commands, opt.facing_right, alloc);

    play_action(f, 0, 0.0f);
}

void fighter_on_input(Fighter* f, u32 action, f32 value)
{
    bool pressed = value > 0.5f;
    switch (action)
    {
        case ACT_MOVE_LEFT:  f->input_left  = pressed; break;
        case ACT_MOVE_RIGHT: f->input_right = pressed; break;
        case ACT_CROUCH:     f->input_down  = pressed; break;
        case ACT_JUMP:       f->input_up    = pressed; break;
        case ACT_BTN_A:      f->btn_a       = pressed; break;
        case ACT_BTN_B:      f->btn_b       = pressed; break;
        case ACT_BTN_C:      f->btn_c       = pressed; break;
        case ACT_BTN_X:      f->btn_x       = pressed; break;
        case ACT_BTN_Y:      f->btn_y       = pressed; break;
        case ACT_BTN_Z:      f->btn_z       = pressed; break;
    }
}

static void cns_enter_state(Fighter* f, i32 stateno);

void fighter_enable_cns(Fighter* f, Mugen_Cns* cns, Mugen_Cns* common_cns, Mugen_Cns* cmd_cns)
{
    f->cns = cns;
    f->common_cns = common_cns;
    f->cmd_cns = cmd_cns;

    Mugen_Char_Constants* c = &cns->constants;

    Mugen_Char_State* st = &f->cns_state;
    memset(st, 0, sizeof(*st));
    st->pos_x = f->x;
    st->pos_y = f->y;
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
    st->run_back_y = c->run_back_y;
    st->jump_neu_x = c->jump_neu_x;
    st->jump_fwd_x = c->jump_fwd_x;
    st->jump_back_x = c->jump_back_x;
    st->jump_y = c->jump_y;
    st->runjump_fwd_x = c->runjump_fwd_x;
    st->runjump_back_x = c->runjump_back_x;
    st->runjump_y = c->runjump_y;
    st->airjump_neu_x = c->airjump_neu_x;
    st->airjump_fwd_x = c->airjump_fwd_x;
    st->airjump_back_x = c->airjump_back_x;
    st->airjump_y = c->airjump_y;
    st->data_attack = (f32)c->attack;
    st->attack_dist = c->attack_dist;
    st->palno = 1;

    f->ground_front = c->ground_front;
    f->ground_back = c->ground_back;

    st->roundstate = 2;
    st->roundno = 1;
    st->roundsexisted = 0;
    st->anim_exists = anim_exists_cb;
    st->anim_exists_ctx = f;

    f->last_cns_anim = UINT32_MAX;
    cns_enter_state(f, 0);
}

static void cns_enter_state(Fighter* f, i32 stateno)
{
    Mugen_Statedef* def = mugen_cns_get(f->cns, stateno);
    if (def)
    {
        mugen_cns_enter_state(f->cns, &f->cns_state, stateno);
        return;
    }
    if (f->common_cns)
    {
        def = mugen_cns_get(f->common_cns, stateno);
        if (def)
        {
            mugen_cns_enter_state(f->common_cns, &f->cns_state, stateno);
            return;
        }
    }
    if (stateno != 0)
        cns_enter_state(f, 0);
}

static void cns_tick_state(Fighter* f)
{
    Mugen_Statedef* def = mugen_cns_get(f->cns, f->cns_state.stateno);
    if (def)
        mugen_cns_tick(f->cns, &f->cns_state);
    else if (f->common_cns)
        mugen_cns_tick(f->common_cns, &f->cns_state);
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
        if ((hold_fwd || hold_back) && st->stateno == 0)
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

static void sync_animtime(Fighter* f)
{
    Mugen_Char_State* st = &f->cns_state;
    if (f->player.chain_count == 0) return;

    Mel_Anim_Clip_State* cs = &f->player.chain[0];
    Mel_Anim_Clip* clip = (Mel_Anim_Clip*)mel_slotmap_get(f->clip_pool, cs->clip);
    if (!clip) return;

    f32 elapsed = cs->time;
    f32 duration = clip->duration;
    if (clip->is_looping)
        st->animtime = -1;
    else
        st->animtime = (i32)((elapsed - duration) * MUGEN_TICKS_PER_SECOND);

    f32 frame_f;
    mel_anim_player_get_float(&f->player, s_frame_prop, f->player.alloc, &frame_f);
    st->animelem = (i32)frame_f + 1;
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

    st->commands = &f->commands;
    st->state_changed = false;
    st->stage_left = stage_left;
    st->stage_right = stage_right;
    st->ground_front = f->ground_front;
    st->ground_back = f->ground_back;

    mel_anim_player_update(&f->player, dt);
    sync_animtime(f);

    i32 prev_stateno = st->stateno;

    {
        Mugen_Statedef* def2 = mugen_cns_get(f->cns, -2);
        if (def2)
            mugen_cns_tick_statedef(def2, st);
    }

    if (st->hitpause_time <= 0)
    {
        {
            Mugen_Statedef* def3 = mugen_cns_get(f->cns, -3);
            if (def3)
                mugen_cns_tick_statedef(def3, st);
        }

        run_statedef_minus1(f);

        if (!st->state_changed)
            engine_movement(f);

        if (!st->state_changed)
        {
            cns_tick_state(f);

            if (st->state_changed)
            {
                if (st->pending_ctrl >= 0)
                    st->ctrl = st->pending_ctrl != 0;
                cns_enter_state(f, st->pending_state);
            }
        }
    }

    if (st->stateno != prev_stateno)
        printf("STATE: %d -> %d (type=%d phys=%d ctrl=%d vel=%.2f,%.2f pos=%.2f,%.2f anim=%d animtime=%d)\n",
            prev_stateno, st->stateno, st->statetype, st->physics, st->ctrl,
            st->vel_x, st->vel_y, st->pos_x, st->pos_y, st->anim, st->animtime);

    f->vel_x = st->vel_x;
    f->vel_y = st->vel_y;
    f->x = st->pos_x;
    f->y = st->pos_y;

    if (f->y < 0.0f) f->y = 0.0f;
    if (f->x < stage_left + f->ground_back) f->x = stage_left + f->ground_back;
    if (f->x > stage_right - f->ground_front) f->x = stage_right - f->ground_front;

    st->pos_x = f->x;
    st->pos_y = f->y;

    if (st->anim != f->last_cns_anim)
    {
        f->last_cns_anim = st->anim;
        f->current_action = UINT32_MAX;
        play_action(f, st->anim, 0.0f);
    }

    sample_anim(f);
}

void fighter_apply_combat_state(Fighter* f)
{
    Mugen_Char_State* st = &f->cns_state;
    if (!st->state_changed) return;

    i32 prev_stateno = st->stateno;
    cns_enter_state(f, st->pending_state);
    st->state_changed = false;

    if (st->stateno != prev_stateno)
        printf("COMBAT STATE: %d -> %d (type=%d phys=%d ctrl=%d)\n",
            prev_stateno, st->stateno, st->statetype, st->physics, st->ctrl);

    if (st->anim != f->last_cns_anim)
    {
        f->last_cns_anim = st->anim;
        f->current_action = UINT32_MAX;
        play_action(f, st->anim, 0.0f);
    }
}

Fighter_Box fighter_hurtbox(Fighter* f)
{
    if (f->anim_hurtbox[2] > 0.0f && f->anim_hurtbox[3] > 0.0f)
    {
        f32 hx = f->anim_hurtbox[0];
        f32 hy = f->anim_hurtbox[1];
        f32 hw = f->anim_hurtbox[2];
        f32 hh = f->anim_hurtbox[3];

        f32 world_x;
        if (f->facing_right)
            world_x = f->x + hx;
        else
            world_x = f->x - hx - hw;

        return (Fighter_Box){ world_x, f->y - hy - hh, hw, hh };
    }

    f32 w = f->ground_front + f->ground_back;
    f32 h = f->cns ? f->cns->constants.height : 60.0f;
    return (Fighter_Box){ f->x - f->ground_back, f->y, w, h };
}

Fighter_Box fighter_hitbox(Fighter* f)
{
    if (f->anim_hitbox[2] <= 0.0f || f->anim_hitbox[3] <= 0.0f)
        return (Fighter_Box){0};

    f32 hx = f->anim_hitbox[0];
    f32 hy = f->anim_hitbox[1];
    f32 hw = f->anim_hitbox[2];
    f32 hh = f->anim_hitbox[3];

    f32 world_x;
    if (f->facing_right)
        world_x = f->x + hx;
    else
        world_x = f->x - hx - hw;

    return (Fighter_Box){ world_x, f->y - hy - hh, hw, hh };
}

bool fighter_has_active_hitbox(Fighter* f)
{
    return f->anim_hitbox[2] > 0.0f
        && f->anim_hitbox[3] > 0.0f;
}
