#include "mugen.match.h"
#include "mugen.char.h"
#include "mugen.command.h"
#include "allocator.h"
#include "sim.ctx.h"
#include <assert.h>
#include <string.h>

static void match_fixed_update(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    (void)sim;
    Mugen_Match* m = user;
    m->p1.input_left  = m->p1_inputs.left;
    m->p1.input_right = m->p1_inputs.right;
    m->p1.input_up    = m->p1_inputs.up;
    m->p1.input_down  = m->p1_inputs.down;
    m->p1.btn_a       = m->p1_inputs.a;
    m->p1.btn_b       = m->p1_inputs.b;
    m->p1.btn_c       = m->p1_inputs.c;
    m->p1.btn_x       = m->p1_inputs.x;
    m->p1.btn_y       = m->p1_inputs.y;
    m->p1.btn_z       = m->p1_inputs.z;

    m->p2.input_left  = m->p2_inputs.left;
    m->p2.input_right = m->p2_inputs.right;
    m->p2.input_up    = m->p2_inputs.up;
    m->p2.input_down  = m->p2_inputs.down;
    m->p2.btn_a       = m->p2_inputs.a;
    m->p2.btn_b       = m->p2_inputs.b;
    m->p2.btn_c       = m->p2_inputs.c;
    m->p2.btn_x       = m->p2_inputs.x;
    m->p2.btn_y       = m->p2_inputs.y;
    m->p2.btn_z       = m->p2_inputs.z;

    mugen_match_tick(m, dt);
}

static void init_fighter(Mugen_Match* m, Fighter* f, Mugen_Char* ch,
                         f32 start_x, bool facing_right)
{
    fighter_init(f, m->alloc,
        .start_x = start_x,
        .facing_right = facing_right,
        .air = &ch->air);

    for (u32 i = 0; i < ch->cmd.command_count; i++)
    {
        Mugen_Cmd_Def* c = &ch->cmd.commands[i];
        command_list_add(&f->commands, c->name, c->command, .time = c->time, .buf_time = 1);
    }

    if (ch->cns_loaded)
        fighter_enable_cns(f, &ch->cns, &ch->common_cns, &ch->cmd_cns);
}

Mugen_Match* mugen_match_create_opt(Mugen_Match_Create_Opt opt)
{
    assert(opt.p1_char);
    assert(opt.p2_char);
    assert(opt.alloc);

    Mugen_Match* m = mel_alloc(opt.alloc, sizeof(Mugen_Match));
    memset(m, 0, sizeof(*m));
    m->alloc = opt.alloc;

    if (opt.stage)
        m->stage = *opt.stage;
    else
        mugen_stage_load(&m->stage, (str8){0}, NULL);

    f32 half_w = opt.screen_w > 0 ? opt.screen_w / 2.0f : 192.0f;
    m->half_screen_w = half_w;

    f32 p1_x = m->stage.p1startx;
    f32 p2_x = m->stage.p2startx;
    bool p1_facing = m->stage.p1facing >= 0;
    bool p2_facing = m->stage.p2facing >= 0;

    f32 stage_left  = m->stage.left_bound + m->stage.screenleft;
    f32 stage_right = m->stage.right_bound - m->stage.screenright;

    mel_sim_init(&m->sim,
        .event_buffer = m->event_buf,
        .event_buffer_size = sizeof(m->event_buf),
        .time_scale = 0.0f,
        .alloc = opt.alloc);

    m->fixed = mel_sim_add_fixed(&m->sim, .fixed_dt = 1.0f / 60.0f);
    mel_sim_fixed_add_update(m->fixed, match_fixed_update, .user = m);

    init_fighter(m, &m->p1, opt.p1_char, p1_x, p1_facing);
    init_fighter(m, &m->p2, opt.p2_char, p2_x, p2_facing);

    m->p1.opponent = &m->p2;
    m->p2.opponent = &m->p1;

    round_init(&m->round,
        .p1 = &m->p1, .p2 = &m->p2,
        .stage_left = stage_left, .stage_right = stage_right);

    mugen_camera_init(&m->cam, &m->stage);

    return m;
}

void mugen_match_start(Mugen_Match* m)
{
    m->sim.time_scale = 1.0f;
}

void mugen_match_pause(Mugen_Match* m)
{
    m->sim.time_scale = 0.0f;
}

void mugen_match_end(Mugen_Match* m)
{
    mel_sim_shutdown(&m->sim);
    fighter_shutdown(&m->p1);
    fighter_shutdown(&m->p2);
    mel_dealloc(m->alloc, m);
}

void mugen_match_update(Mugen_Match* m, f32 frame_dt)
{
    mel_sim_tick(&m->sim, frame_dt);
}

void mugen_match_set_inputs(Mugen_Match* m, Mugen_Player_Inputs p1, Mugen_Player_Inputs p2)
{
    assert(m);
    m->p1_inputs = p1;
    m->p2_inputs = p2;
}

void mugen_match_set_player_inputs(Mugen_Match* m, u32 player_index, Mugen_Player_Inputs inputs)
{
    assert(m);

    if (player_index == MUGEN_MATCH_PLAYER_1)
        m->p1_inputs = inputs;
    else if (player_index == MUGEN_MATCH_PLAYER_2)
        m->p2_inputs = inputs;
    else
        assert(false);
}

Mugen_Player_Inputs mugen_match_get_player_inputs(Mugen_Match* m, u32 player_index)
{
    assert(m);

    if (player_index == MUGEN_MATCH_PLAYER_1)
        return m->p1_inputs;
    if (player_index == MUGEN_MATCH_PLAYER_2)
        return m->p2_inputs;

    assert(false);
    return (Mugen_Player_Inputs){0};
}

void mugen_match_tick(Mugen_Match* m, f32 dt)
{
    round_tick(&m->round, dt);
    mugen_camera_update(&m->cam, m->p1.x, m->p2.x, m->p1.y, m->p2.y, m->half_screen_w);
}

Fighter*      mugen_match_p1(Mugen_Match* m) { return &m->p1; }
Fighter*      mugen_match_p2(Mugen_Match* m) { return &m->p2; }
Round_Ctx*    mugen_match_round(Mugen_Match* m) { return &m->round; }
Mugen_Camera* mugen_match_camera(Mugen_Match* m) { return &m->cam; }
