#pragma once

#include <core/types.h>
#include <collection.slotmap/slotmap.fwd.h>

typedef struct Mel_Track_Group Mel_Track_Group;
typedef struct Mel_Event_Group Mel_Event_Group;
typedef struct Mel_Anim_Clip Mel_Anim_Clip;
typedef struct Mel_Anim_Event Mel_Anim_Event;

typedef Mel_SlotMap Mel_Anim_Clip_Pool;
typedef Mel_SlotMap_Handle Mel_Anim_Clip_Handle;

#define MEL_ANIM_CLIP_HANDLE_NULL MEL_SLOTMAP_HANDLE_NULL

#define MEL_ANIM_ADDITIVE_LOCAL 0
#define MEL_ANIM_ADDITIVE_MESH  1
