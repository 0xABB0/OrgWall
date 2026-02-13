#pragma once

#include "core.types.h"
#include "allocator.fwd.h"
#include "anim.state.fwd.h"
#include "anim.timeline.fwd.h"
#include "anim.timeline.h"

#define MEL_ANIM_TRANSITION_IMMEDIATE 0
#define MEL_ANIM_TRANSITION_AT_END    1

struct Mel_Anim_Transition {
    u32 target_state;
    u32 mode;
    u64 condition_hash;
    f32 blend_duration;
};

struct Mel_Anim_State_Def {
    u64 name_hash;
    Mel_Anim_Timeline* timeline;
    Mel_Anim_Transition* transitions;
    u32 transition_count;
};

struct Mel_Anim_State_Machine {
    Mel_Anim_State_Def* states;
    u32 state_count;
    u32 initial_state;
    const Mel_Alloc* alloc;
};

struct Mel_Anim_State_Player {
    Mel_Anim_State_Machine* machine;
    u32 current_state;
    Mel_Anim_Playback playback;
    u64* active_conditions;
    u32 active_condition_count;
};

void mel_anim_state_player_init(Mel_Anim_State_Player* player, Mel_Anim_State_Machine* machine);
void mel_anim_state_player_update(Mel_Anim_State_Player* player, f32 dt);
void mel_anim_state_player_set_condition(Mel_Anim_State_Player* player, u64 hash, bool active);
u32  mel_anim_state_player_frame_index(Mel_Anim_State_Player* player);
