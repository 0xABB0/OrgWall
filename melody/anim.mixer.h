#pragma once

#include "core.types.h"
#include "anim.mixer.fwd.h"
#include "anim.clip.fwd.h"
#include "anim.track.fwd.h"
#include "event.channel.h"
#include "allocator.fwd.h"
#include "collection.array.fwd.h"

#define MEL_ANIM_MIX_REPLACE 0
#define MEL_ANIM_MIX_ADD     1

#define MEL_ANIM_EVENT_STARTED     0xFFFFFFFFFFFF0001ULL
#define MEL_ANIM_EVENT_COMPLETED   0xFFFFFFFFFFFF0002ULL
#define MEL_ANIM_EVENT_LOOPED      0xFFFFFFFFFFFF0003ULL
#define MEL_ANIM_EVENT_INTERRUPTED 0xFFFFFFFFFFFF0004ULL

struct Mel_Anim_Fired_Event {
    u64 event_id;
    f32 float_value;
    u64 user_data;
    u32 layer_index;
    f32 time;
};

struct Mel_Anim_Queue_Entry {
    Mel_Anim_Clip* clip;
    f32 mix_duration;
};

struct Mel_Anim_Mix_Entry {
    Mel_Anim_Clip* clip;
    f32 time;
    f32 speed;
    f32 mix_duration;
    f32 mix_elapsed;
    Mel_Anim_Mix_Entry* from;
};

struct Mel_Anim_Layer {
    Mel_Anim_Clip* clip;
    f32 time;
    f32 weight;
    f32 speed;
    u32 mix_mode;
    bool playing;
    bool finished;

    Mel_Anim_Mix_Entry* mix_from;
    f32 mix_duration;
    f32 mix_elapsed;

    Mel_Array(Mel_Anim_Queue_Entry) queue;
};

struct Mel_Anim_Mixer_Output {
    u64 property_id;
    u32 stride;
    Mel_Blend_Fn blend;
    f32 value[4];
};

struct Mel_Anim_Mixer_Default {
    u64 property_id;
    u32 stride;
    Mel_Blend_Fn blend;
    f32 value[4];
};

struct Mel_Anim_Mixer {
    Mel_Array(Mel_Anim_Layer) layers;
    Mel_Array(Mel_Anim_Mixer_Output) outputs;
    Mel_Array(Mel_Anim_Fired_Event) pending_events;
    Mel_Array(Mel_Anim_Mixer_Default) defaults;
    Mel_Event_Channel events;
    const Mel_Alloc* alloc;
};

void mel_anim_mixer_init(Mel_Anim_Mixer* mixer, const Mel_Alloc* alloc);
void mel_anim_mixer_destroy(Mel_Anim_Mixer* mixer);

u32  mel_anim_mixer_add_layer(Mel_Anim_Mixer* mixer);
Mel_Anim_Layer* mel_anim_mixer_layer(Mel_Anim_Mixer* mixer, u32 index);

void mel_anim_mixer_play(Mel_Anim_Mixer* mixer, u32 layer,
                         Mel_Anim_Clip* clip, f32 mix_duration);
void mel_anim_mixer_stop(Mel_Anim_Mixer* mixer, u32 layer);
void mel_anim_mixer_queue(Mel_Anim_Mixer* mixer, u32 layer,
                          Mel_Anim_Clip* clip, f32 mix_duration);

void mel_anim_mixer_set_default(Mel_Anim_Mixer* mixer, u64 property_id,
                                u32 stride, const f32* value, Mel_Blend_Fn blend);

void mel_anim_mixer_update(Mel_Anim_Mixer* mixer, f32 dt);

u32  mel_anim_mixer_output_count(const Mel_Anim_Mixer* mixer);
const Mel_Anim_Mixer_Output* mel_anim_mixer_output(const Mel_Anim_Mixer* mixer, u32 index);
const Mel_Anim_Mixer_Output* mel_anim_mixer_find_output(const Mel_Anim_Mixer* mixer, u64 property_id);

void mel_anim_mixer_flush_events(Mel_Anim_Mixer* mixer);
