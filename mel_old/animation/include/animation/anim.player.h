#pragma once

#include <core/types.h>
#include "anim.player.fwd.h"
#include "anim.clip.fwd.h"
#include "anim.pose.fwd.h"
#include "anim.skeleton.fwd.h"
#include <allocator/fwd.h>
#include <math.vector/vec3.h>
#include <math.vector/quat.h>

struct Mel_Anim_Clip_State {
    Mel_Anim_Clip_Handle clip;
    f32 time;
    f32 speed;
    u32* cursors;
    u32 cursor_count;
    f32 xfade_elapsed;
    f32 xfade_duration;
};

struct Mel_Anim_Player {
    const Mel_Alloc* alloc;
    Mel_Anim_Clip_Pool* clip_pool;

    Mel_Anim_Clip_State* chain;
    u32 chain_count;
    u32 chain_capacity;

    f32 phase;

    Mel_Anim_Event* pending_events;
    u32 event_count;
    u32 event_capacity;

    Mel_Vec3 prev_root_pos;
    Mel_Quat prev_root_rot;
};

struct Mel_Root_Motion {
    Mel_Vec3 delta_position;
    Mel_Quat delta_rotation;
};

void mel_anim_player_init(Mel_Anim_Player* player, const Mel_Alloc* alloc,
                          Mel_Anim_Clip_Pool* clip_pool);
void mel_anim_player_destroy(Mel_Anim_Player* player);

typedef struct {
    f32 crossfade;
    f32 start_time;
    f32 speed;
} Mel_Anim_Play_Opt;

void mel_anim_player_play_opt(Mel_Anim_Player* player,
                              Mel_Anim_Clip_Handle clip,
                              Mel_Anim_Play_Opt opt);
#define mel_anim_player_play(player, clip, ...) \
    mel_anim_player_play_opt((player), (clip), \
    (Mel_Anim_Play_Opt){ .speed = 1.0f, __VA_ARGS__ })

void mel_anim_player_update(Mel_Anim_Player* player, f32 dt);

void mel_anim_player_sample(Mel_Anim_Player* player,
                            const Mel_Alloc* scratch,
                            Mel_Local_Pose* out_pose);

void mel_anim_player_get_float(Mel_Anim_Player* player, u64 property_id,
                               const Mel_Alloc* scratch, f32* out);
void mel_anim_player_get_vec2(Mel_Anim_Player* player, u64 property_id,
                              const Mel_Alloc* scratch, void* out);
void mel_anim_player_get_vec3(Mel_Anim_Player* player, u64 property_id,
                              const Mel_Alloc* scratch, void* out);
void mel_anim_player_get_vec4(Mel_Anim_Player* player, u64 property_id,
                              const Mel_Alloc* scratch, void* out);
void mel_anim_player_get_quat(Mel_Anim_Player* player, u64 property_id,
                              const Mel_Alloc* scratch, void* out);

void mel_anim_player_extract_root_motion(Mel_Anim_Player* player,
                                         const Mel_Local_Pose* pose,
                                         const Mel_Skeleton* skeleton,
                                         Mel_Root_Motion* out);
