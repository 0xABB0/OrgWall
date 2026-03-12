#pragma once

#include "core.types.h"
#include "mugen.fighter.h"
#include "mugen.round.h"
#include "mugen.stage.h"
#include "mugen.camera.h"
#include "sim.ctx.h"
#include "mugen.char.h"
#include "allocator.fwd.h"

typedef struct {
    bool left, right, up, down;
    bool a, b, c;
    bool x, y, z;
} Mugen_Player_Inputs;

enum {
    MUGEN_MATCH_PLAYER_1 = 0,
    MUGEN_MATCH_PLAYER_2 = 1,
};

typedef struct {
    Fighter p1;
    Fighter p2;
    Round_Ctx round;
    Mugen_Stage stage;
    Mugen_Camera cam;
    Mel_Sim_Ctx sim;
    Mel_Sim_Fixed* fixed;
    u8 event_buf[1024];
    const Mel_Alloc* alloc;
    f32 half_screen_w;
    Mugen_Player_Inputs p1_inputs;
    Mugen_Player_Inputs p2_inputs;
} Mugen_Match;

typedef struct {
    Mugen_Char* p1_char;
    Mugen_Char* p2_char;
    Mugen_Stage* stage;
    f32 screen_w;
    const Mel_Alloc* alloc;
} Mugen_Match_Create_Opt;

Mugen_Match* mugen_match_create_opt(Mugen_Match_Create_Opt opt);
#define mugen_match_create(...) mugen_match_create_opt((Mugen_Match_Create_Opt){__VA_ARGS__})

void mugen_match_start(Mugen_Match* m);
void mugen_match_pause(Mugen_Match* m);
void mugen_match_end(Mugen_Match* m);

void mugen_match_update(Mugen_Match* m, f32 frame_dt);
void mugen_match_tick(Mugen_Match* m, f32 dt);
void mugen_match_set_inputs(Mugen_Match* m, Mugen_Player_Inputs p1, Mugen_Player_Inputs p2);
void mugen_match_set_player_inputs(Mugen_Match* m, u32 player_index, Mugen_Player_Inputs inputs);
Mugen_Player_Inputs mugen_match_get_player_inputs(Mugen_Match* m, u32 player_index);

Fighter*      mugen_match_p1(Mugen_Match* m);
Fighter*      mugen_match_p2(Mugen_Match* m);
Round_Ctx*    mugen_match_round(Mugen_Match* m);
Mugen_Camera* mugen_match_camera(Mugen_Match* m);
