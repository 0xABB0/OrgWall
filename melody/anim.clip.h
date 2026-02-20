#pragma once

#include "core.types.h"
#include "anim.clip.fwd.h"
#include "anim.track.fwd.h"
#include "allocator.fwd.h"

struct Mel_Anim_Clip_Event {
    f32 time;
    u64 event_id;
    f32 float_value;
    u64 user_data;
};

struct Mel_Anim_Clip {
    u64 name_hash;
    Mel_Anim_Track* tracks;
    u32 track_count;
    Mel_Anim_Clip_Event* events;
    u32 event_count;
    f32 duration;
    bool loop;
};

void mel_anim_clip_init(Mel_Anim_Clip* clip, const Mel_Alloc* alloc,
                        u64 name_hash, u32 track_count, u32 event_count,
                        f32 duration, bool loop);
void mel_anim_clip_destroy(Mel_Anim_Clip* clip, const Mel_Alloc* alloc);

Mel_Anim_Track* mel_anim_clip_track(Mel_Anim_Clip* clip, u32 index);
Mel_Anim_Track* mel_anim_clip_find_track(Mel_Anim_Clip* clip, u64 property_id);
bool            mel_anim_clip_has_property(const Mel_Anim_Clip* clip, u64 property_id);
