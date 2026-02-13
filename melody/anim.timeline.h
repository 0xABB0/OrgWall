#pragma once

#include "core.types.h"
#include "anim.timeline.fwd.h"

struct Mel_Anim_Keyframe {
    u32 frame_index;
    f32 duration;
};

#define MEL_ANIM_EVENT_SOUND  (1 << 0)
#define MEL_ANIM_EVENT_HITBOX (1 << 1)
#define MEL_ANIM_EVENT_TAG    (1 << 2)

struct Mel_Anim_Event {
    u32 flags;
    u64 sound_path_hash;
    f32 sound_volume;
    i32 hitbox_x, hitbox_y;
    u32 hitbox_width, hitbox_height;
    u64 tag_hash;
};

struct Mel_Anim_Timeline {
    Mel_Anim_Keyframe* keyframes;
    Mel_Anim_Event* events;
    u32 keyframe_count;
    bool loop;
};

struct Mel_Anim_Playback {
    Mel_Anim_Timeline* timeline;
    u32 current_keyframe;
    f32 timer;
    bool playing;
    bool finished;
};

void mel_anim_playback_start(Mel_Anim_Playback* pb, Mel_Anim_Timeline* timeline);
void mel_anim_playback_update(Mel_Anim_Playback* pb, f32 dt);
void mel_anim_playback_stop(Mel_Anim_Playback* pb);
Mel_Anim_Event* mel_anim_playback_event(Mel_Anim_Playback* pb);
u32 mel_anim_playback_frame_index(Mel_Anim_Playback* pb);
