#pragma once

#include "core.types.h"
#include "anim.state.fwd.h"
#include "anim.mixer.fwd.h"
#include "anim.clip.fwd.h"
#include "allocator.fwd.h"
#include "collection.array.fwd.h"

#define MEL_ANIM_TRANSITION_IMMEDIATE 0
#define MEL_ANIM_TRANSITION_AT_END    1

struct Mel_Anim_Transition {
    u32 target_state;
    u32 mode;
    u64 condition_hash;
    f32 mix_duration;
    bool auto_clear_condition;
};

struct Mel_Anim_State_Def {
    u64 name_hash;
    Mel_Anim_Clip* clip;
    Mel_Anim_Transition* transitions;
    u32 transition_count;
};

struct Mel_Anim_State_Machine {
    Mel_Anim_State_Def* states;
    u32 state_count;
    u32 initial_state;
};

struct Mel_Anim_State_Player {
    Mel_Anim_State_Machine* machine;
    Mel_Anim_Mixer* mixer;
    u32 mixer_layer;
    u32 current_state;
    Mel_Array(u64) active_conditions;
    Mel_Anim_Transition_Cb on_transition;
    void* on_transition_ctx;
};

void mel_anim_state_machine_init(Mel_Anim_State_Machine* machine,
                                 Mel_Anim_State_Def* states, u32 state_count,
                                 u32 initial_state);
u32  mel_anim_state_machine_find_state(Mel_Anim_State_Machine* machine, u64 name_hash);

typedef struct {
    Mel_Anim_State_Machine* machine;
    Mel_Anim_Mixer* mixer;
    u32 mixer_layer;
    Mel_Anim_Transition_Cb on_transition;
    void* on_transition_ctx;
} Mel_Anim_State_Player_Opt;

void mel_anim_state_player_init_opt(Mel_Anim_State_Player* player,
                                    const Mel_Alloc* alloc,
                                    Mel_Anim_State_Player_Opt opt);
#define mel_anim_state_player_init(player, alloc, ...) \
    mel_anim_state_player_init_opt((player), (alloc), \
    (Mel_Anim_State_Player_Opt){__VA_ARGS__})

void mel_anim_state_player_destroy(Mel_Anim_State_Player* player);
void mel_anim_state_player_update(Mel_Anim_State_Player* player);
void mel_anim_state_player_set_condition(Mel_Anim_State_Player* player,
                                         u64 hash, bool active);
bool mel_anim_state_player_has_condition(const Mel_Anim_State_Player* player,
                                         u64 hash);
u32  mel_anim_state_player_current_state(const Mel_Anim_State_Player* player);
