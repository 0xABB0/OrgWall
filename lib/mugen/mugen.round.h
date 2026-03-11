#pragma once

#include "core.types.h"

typedef struct Fighter Fighter;

#define ROUND_PRE_INTRO  0
#define ROUND_INTRO      1
#define ROUND_FIGHT      2
#define ROUND_KO         3
#define ROUND_POST       4

typedef struct {
    u8 state;
    i32 timer;
    i32 roundno;
    i32 ko_pause_ticks;
    i32 p1_wins;
    i32 p2_wins;
    i32 rounds_to_win;
    Fighter* p1;
    Fighter* p2;
    Fighter* ko_fighter;
    Fighter* winner;
    f32 stage_left;
    f32 stage_right;
} Round_Ctx;

typedef struct {
    Fighter* p1;
    Fighter* p2;
    i32 rounds_to_win;
    i32 ko_pause_ticks;
    f32 stage_left;
    f32 stage_right;
} Round_Init_Opt;

void round_init_opt(Round_Ctx* r, Round_Init_Opt opt);
#define round_init(r, ...) round_init_opt((r), (Round_Init_Opt){__VA_ARGS__})

void round_tick(Round_Ctx* r, f32 dt);
void round_reset(Round_Ctx* r);
