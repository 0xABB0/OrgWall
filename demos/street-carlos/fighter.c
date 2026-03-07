#include "fighter.h"
#include "string.str8.h"
#include <string.h>

enum {
    ACT_MOVE_LEFT = 1,
    ACT_MOVE_RIGHT,
    ACT_CROUCH,
    ACT_JUMP,
    ACT_PUNCH,
};

static void spawn_projectile(Fighter* f, Move_Def* move)
{
    for (u32 i = 0; i < MAX_PROJECTILES; i++)
    {
        if (!f->projectiles[i].active)
        {
            f32 speed = move->projectile_speed;
            f->projectiles[i] = (Projectile){
                .x = f->facing_right ? f->x + f->character->width : f->x - 12.0f,
                .y = 0.0f,
                .vel_x = f->facing_right ? speed : -speed,
                .hit_w = 12.0f,
                .hit_h = 12.0f,
                .damage = move->damage,
                .hitstun = move->hitstun,
                .active = true,
            };
            return;
        }
    }
}

static void enter_move(Fighter* f, Move_Def* move)
{
    f->current_move = move;
    f->move_frame = 0;
    f->move_phase = MOVE_PHASE_STARTUP;
    f->hit_confirmed = false;

    if (move->airborne)
    {
        f->vel_y = move->launch_vel_y;
        f->vel_x = f->facing_right ? move->launch_vel_x : -move->launch_vel_x;
    }

    if (move->spawns_projectile)
        spawn_projectile(f, move);
}

static void exit_move(Fighter* f)
{
    f->current_move = NULL;
    f->move_phase = MOVE_PHASE_NONE;
    f->move_frame = 0;

    if (f->y > 0.0f)
        f->locomotion = LOCO_JUMP;
    else
        f->locomotion = LOCO_IDLE;
}

static bool move_requirements_met(Fighter* f, Move_Def* move)
{
    bool on_ground = f->y <= 0.0f;

    if (move->requires_ground && !on_ground) return false;
    if (move->requires_air && on_ground) return false;
    if (move->requires_crouch && f->locomotion != LOCO_CROUCH) return false;

    return true;
}

static int move_priority_cmp(const void* a, const void* b)
{
    const Move_Def* ma = *(const Move_Def**)a;
    const Move_Def* mb = *(const Move_Def**)b;
    if (ma->priority > mb->priority) return -1;
    if (ma->priority < mb->priority) return 1;
    return 0;
}

static void try_moves(Fighter* f)
{
    Move_Def* sorted[64];
    u32 count = f->move_count < 64 ? f->move_count : 64;

    for (u32 i = 0; i < count; i++)
        sorted[i] = &f->moves[i];

    qsort(sorted, count, sizeof(Move_Def*), move_priority_cmp);

    for (u32 i = 0; i < count; i++)
    {
        if (!move_requirements_met(f, sorted[i])) continue;
        if (!command_list_active(&f->commands, sorted[i]->command_name)) continue;

        enter_move(f, sorted[i]);
        return;
    }
}

static void tick_move(Fighter* f, f32 dt, f32 stage_left, f32 stage_right)
{
    Move_Def* move = f->current_move;

    f->move_frame++;
    f->move_phase = move_phase_at_frame(move, f->move_frame);

    if (move->airborne)
    {
        f->vel_y -= f->character->gravity * dt;
        f->y += f->vel_y * dt;
        f->x += f->vel_x * dt;

        if (f->y <= 0.0f)
        {
            f->y = 0.0f;
            f->vel_y = 0.0f;
            f->vel_x = 0.0f;
            exit_move(f);
            return;
        }
    }

    if (f->move_phase == MOVE_PHASE_NONE)
    {
        exit_move(f);
        return;
    }

    if (f->x < stage_left) f->x = stage_left;
    if (f->x > stage_right - f->character->width) f->x = stage_right - f->character->width;
}

static void tick_locomotion(Fighter* f, f32 dt, f32 stage_left, f32 stage_right)
{
    Input_Buffer* buf = &f->commands.buffer;

    if (f->locomotion == LOCO_JUMP)
    {
        f->vel_y -= f->character->gravity * dt;
        f->y += f->vel_y * dt;

        bool forward = buf->Fb > 0;
        bool back = buf->Bb > 0;
        if (forward) f->x += f->character->walk_speed * dt;
        if (back) f->x -= f->character->walk_speed * dt;

        if (f->y <= 0.0f)
        {
            f->y = 0.0f;
            f->vel_y = 0.0f;
            f->locomotion = LOCO_IDLE;
        }

        if (f->x < stage_left) f->x = stage_left;
        if (f->x > stage_right - f->character->width) f->x = stage_right - f->character->width;
        return;
    }

    bool holding_down = buf->Db > 0;
    bool holding_up = buf->Ub > 0;
    bool forward = buf->Fb > 0;
    bool back = buf->Bb > 0;

    if (holding_up)
    {
        f->vel_y = f->character->jump_vel;
        f->locomotion = LOCO_JUMP;
        return;
    }

    if (holding_down)
    {
        f->locomotion = LOCO_CROUCH;
        return;
    }

    if (forward && !back)
    {
        f->x += f->character->walk_speed * dt;
        f->locomotion = LOCO_WALK_FORWARD;
    }
    else if (back && !forward)
    {
        f->x -= f->character->walk_speed * dt;
        f->locomotion = LOCO_WALK_BACK;
    }
    else
    {
        f->locomotion = LOCO_IDLE;
    }

    if (f->x < stage_left) f->x = stage_left;
    if (f->x > stage_right - f->character->width) f->x = stage_right - f->character->width;
}

void fighter_init(Fighter* f, Character_Def* character, Move_Def* moves, u32 move_count,
                  f32 start_x, bool facing_right, const Mel_Alloc* alloc)
{
    memset(f, 0, sizeof(*f));
    f->character = character;
    f->moves = moves;
    f->move_count = move_count;
    f->x = start_x;
    f->facing_right = facing_right;
    f->health = 100.0f;

    command_list_init(&f->commands, facing_right, alloc);

    command_list_add(&f->commands, S8("LP"), S8("a"), .time = 1, .buf_time = 1);

    command_list_add(&f->commands, S8("Hadouken"), S8("~D, DF, F, a"), .time = 15, .buf_time = 1);
    command_list_add(&f->commands, S8("Hadouken"), S8("~D, DF, F, b"), .time = 15, .buf_time = 1);
    command_list_add(&f->commands, S8("Hadouken"), S8("~D, DF, F, c"), .time = 15, .buf_time = 1);

    command_list_add(&f->commands, S8("Shoryuken"), S8("~F, D, DF, a"), .time = 15, .buf_time = 1);
    command_list_add(&f->commands, S8("Shoryuken"), S8("~F, D, DF, b"), .time = 15, .buf_time = 1);
    command_list_add(&f->commands, S8("Shoryuken"), S8("~F, D, DF, c"), .time = 15, .buf_time = 1);
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
        case ACT_PUNCH:      f->btn_a       = pressed; break;
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

    for (u32 i = 0; i < MAX_PROJECTILES; i++)
    {
        if (!f->projectiles[i].active) continue;
        f->projectiles[i].x += f->projectiles[i].vel_x * dt;
        if (f->projectiles[i].x < stage_left - 20.0f || f->projectiles[i].x > stage_right + 20.0f)
            f->projectiles[i].active = false;
    }

    if (f->hitstun_remaining > 0)
    {
        f->hitstun_remaining--;
        if (f->hitstun_remaining == 0)
            f->locomotion = f->y > 0.0f ? LOCO_JUMP : LOCO_IDLE;
        return;
    }

    if (f->current_move)
    {
        tick_move(f, dt, stage_left, stage_right);
        return;
    }

    if (f->locomotion != LOCO_JUMP)
        try_moves(f);

    if (!f->current_move)
        tick_locomotion(f, dt, stage_left, stage_right);
}

void fighter_take_hit(Fighter* f, f32 damage, f32 knockback_x, f32 knockback_y, u32 hitstun)
{
    f->health -= damage;
    f->vel_x = knockback_x;
    f->vel_y = knockback_y;
    f->hitstun_remaining = hitstun;
    f->locomotion = LOCO_HITSTUN;
    f->current_move = NULL;
    f->move_phase = MOVE_PHASE_NONE;
}

Fighter_Box fighter_hurtbox(Fighter* f)
{
    Character_Def* c = f->character;

    f32 hx, hy, hw, hh;

    if (f->locomotion == LOCO_CROUCH && !f->current_move)
    {
        hx = c->crouch_hurt_x;
        hy = c->crouch_hurt_y;
        hw = c->crouch_hurt_w;
        hh = c->crouch_hurt_h;
    }
    else
    {
        hx = c->hurt_x;
        hy = c->hurt_y;
        hw = c->hurt_w;
        hh = c->hurt_h;
    }

    f32 world_x;
    if (f->facing_right)
        world_x = f->x + hx;
    else
        world_x = f->x + c->width - hx - hw;

    return (Fighter_Box){ world_x, f->y + hy, hw, hh };
}

Fighter_Box fighter_hitbox(Fighter* f)
{
    if (!f->current_move || f->move_phase != MOVE_PHASE_ACTIVE)
        return (Fighter_Box){0};

    Move_Def* m = f->current_move;

    f32 world_x;
    if (f->facing_right)
        world_x = f->x + m->hit_x;
    else
        world_x = f->x + f->character->width - m->hit_x - m->hit_w;

    return (Fighter_Box){ world_x, f->y + m->hit_y, m->hit_w, m->hit_h };
}

bool fighter_has_active_hitbox(Fighter* f)
{
    return f->current_move && f->move_phase == MOVE_PHASE_ACTIVE && !f->hit_confirmed;
}
