#pragma once

#include "core.types.h"

typedef struct Mel_Anim_Transition Mel_Anim_Transition;
typedef struct Mel_Anim_State_Def Mel_Anim_State_Def;
typedef struct Mel_Anim_State_Machine Mel_Anim_State_Machine;
typedef struct Mel_Anim_State_Player Mel_Anim_State_Player;

typedef void (*Mel_Anim_Transition_Cb)(void* ctx, u32 from_state, u32 to_state);
