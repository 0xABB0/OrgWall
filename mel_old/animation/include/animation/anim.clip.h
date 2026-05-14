#pragma once

#include <core/types.h>
#include "anim.clip.fwd.h"
#include <allocator/fwd.h>

struct Mel_Track_Group {
    u64 type_hash;
    u32 track_count;

    u64* property_ids;
    u32* keyframe_counts;
    u32* data_offsets;

    f32* flat_times;
    void* flat_values;
    u16* flat_easing_ids;
    f32* flat_easing_params;
};

struct Mel_Event_Group {
    u32 track_count;
    u64* property_ids;
    u32* keyframe_counts;
    u32* data_offsets;

    f32* flat_times;
    u64* flat_event_hashes;
};

struct Mel_Anim_Event {
    u64 event_hash;
    u64 property_id;
    f32 time;
};

struct Mel_Anim_Clip {
    u64 name_hash;
    f32 duration;
    bool is_looping;
    f32 loop_start_time;
    u32 additive_space;

    Mel_Track_Group* groups;
    u32 group_count;

    Mel_Event_Group* event_groups;
    u32 event_group_count;
};

void mel_anim_clip_destroy(Mel_Anim_Clip* clip, const Mel_Alloc* alloc);
u32  mel_anim_clip_cursor_count(const Mel_Anim_Clip* clip);
