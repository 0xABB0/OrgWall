#include "anim.mixer.h"
#include "anim.clip.h"
#include "anim.track.h"
#include "allocator.h"
#include "collection.array.h"

#include <string.h>

static void mel__mixer_free_chain(const Mel_Alloc* alloc, Mel_Anim_Mix_Entry* entry)
{
    while (entry)
    {
        Mel_Anim_Mix_Entry* next = entry->from;
        mel_dealloc(alloc, entry);
        entry = next;
    }
}

static void mel__mixer_fire_lifecycle(Mel_Anim_Mixer* mixer, u32 layer_index,
                                       u64 event_type, f32 time)
{
    mel_array_push(&mixer->pending_events, ((Mel_Anim_Fired_Event){
        .event_id = event_type,
        .float_value = 0.0f,
        .user_data = 0,
        .layer_index = layer_index,
        .time = time,
    }));
}

void mel_anim_mixer_init(Mel_Anim_Mixer* mixer, const Mel_Alloc* alloc)
{
    assert(mixer != NULL);
    assert(alloc != NULL);

    mixer->alloc = alloc;
    mel_array_init(&mixer->layers, alloc);
    mel_array_init(&mixer->outputs, alloc);
    mel_array_init(&mixer->pending_events, alloc);
    mel_array_init(&mixer->defaults, alloc);
    mel_event_channel_init(&mixer->events, alloc);
}

void mel_anim_mixer_destroy(Mel_Anim_Mixer* mixer)
{
    assert(mixer != NULL);

    for (usize i = 0; i < mixer->layers.count; i++)
    {
        Mel_Anim_Layer* l = &mixer->layers.items[i];
        mel__mixer_free_chain(mixer->alloc, l->mix_from);
        mel_array_free(&l->queue);
    }

    mel_array_free(&mixer->layers);
    mel_array_free(&mixer->outputs);
    mel_array_free(&mixer->pending_events);
    mel_array_free(&mixer->defaults);
    mel_event_channel_destroy(&mixer->events);
    mixer->alloc = NULL;
}

u32 mel_anim_mixer_add_layer(Mel_Anim_Mixer* mixer)
{
    assert(mixer != NULL);

    Mel_Anim_Layer layer = {
        .clip = NULL,
        .time = 0.0f,
        .weight = 1.0f,
        .speed = 1.0f,
        .mix_mode = MEL_ANIM_MIX_REPLACE,
        .playing = false,
        .finished = false,
        .mix_from = NULL,
        .mix_duration = 0.0f,
        .mix_elapsed = 0.0f,
    };
    mel_array_init(&layer.queue, mixer->alloc);

    u32 index = (u32)mixer->layers.count;
    mel_array_push(&mixer->layers, layer);
    return index;
}

Mel_Anim_Layer* mel_anim_mixer_layer(Mel_Anim_Mixer* mixer, u32 index)
{
    assert(mixer != NULL);
    assert(index < mixer->layers.count);
    return &mixer->layers.items[index];
}

void mel_anim_mixer_play(Mel_Anim_Mixer* mixer, u32 layer,
                         Mel_Anim_Clip* clip, f32 mix_duration)
{
    assert(mixer != NULL);
    assert(layer < mixer->layers.count);
    assert(clip != NULL);

    Mel_Anim_Layer* l = &mixer->layers.items[layer];

    if (l->clip && l->playing)
        mel__mixer_fire_lifecycle(mixer, layer, MEL_ANIM_EVENT_INTERRUPTED, l->time);

    if (l->clip && mix_duration > 0.0f)
    {
        Mel_Anim_Mix_Entry* entry = mel_alloc_type(mixer->alloc, Mel_Anim_Mix_Entry);
        entry->clip = l->clip;
        entry->time = l->time;
        entry->speed = l->speed;
        entry->mix_duration = l->mix_duration;
        entry->mix_elapsed = l->mix_elapsed;
        entry->from = l->mix_from;

        l->mix_from = entry;
        l->mix_duration = mix_duration;
        l->mix_elapsed = 0.0f;
    }
    else
    {
        mel__mixer_free_chain(mixer->alloc, l->mix_from);
        l->mix_from = NULL;
        l->mix_duration = 0.0f;
        l->mix_elapsed = 0.0f;
    }

    l->clip = clip;
    l->time = 0.0f;
    l->playing = true;
    l->finished = false;

    mel_array_clear(&l->queue);

    mel__mixer_fire_lifecycle(mixer, layer, MEL_ANIM_EVENT_STARTED, 0.0f);
}

void mel_anim_mixer_stop(Mel_Anim_Mixer* mixer, u32 layer)
{
    assert(mixer != NULL);
    assert(layer < mixer->layers.count);

    Mel_Anim_Layer* l = &mixer->layers.items[layer];

    if (l->playing)
        mel__mixer_fire_lifecycle(mixer, layer, MEL_ANIM_EVENT_INTERRUPTED, l->time);

    l->playing = false;
    mel__mixer_free_chain(mixer->alloc, l->mix_from);
    l->mix_from = NULL;
}

void mel_anim_mixer_queue(Mel_Anim_Mixer* mixer, u32 layer,
                          Mel_Anim_Clip* clip, f32 mix_duration)
{
    assert(mixer != NULL);
    assert(layer < mixer->layers.count);
    assert(clip != NULL);

    Mel_Anim_Layer* l = &mixer->layers.items[layer];
    mel_array_push(&l->queue, ((Mel_Anim_Queue_Entry){
        .clip = clip,
        .mix_duration = mix_duration,
    }));
}

void mel_anim_mixer_set_default(Mel_Anim_Mixer* mixer, u64 property_id,
                                u32 stride, const f32* value, Mel_Blend_Fn blend)
{
    assert(mixer != NULL);
    assert(value != NULL);
    assert(stride <= 4);

    for (usize i = 0; i < mixer->defaults.count; i++)
    {
        if (mixer->defaults.items[i].property_id == property_id)
        {
            mixer->defaults.items[i].stride = stride;
            mixer->defaults.items[i].blend = blend;
            memcpy(mixer->defaults.items[i].value, value, sizeof(f32) * stride);
            return;
        }
    }

    Mel_Anim_Mixer_Default def = {
        .property_id = property_id,
        .stride = stride,
        .blend = blend,
        .value = {0},
    };
    memcpy(def.value, value, sizeof(f32) * stride);
    mel_array_push(&mixer->defaults, def);
}

static void mel__mixer_collect_events(Mel_Anim_Mixer* mixer, Mel_Anim_Clip* clip,
                                       f32 prev_time, f32 new_time, u32 layer_index)
{
    for (u32 i = 0; i < clip->event_count; i++)
    {
        f32 et = clip->events[i].time;
        if (et > prev_time && et <= new_time)
        {
            mel_array_push(&mixer->pending_events, ((Mel_Anim_Fired_Event){
                .event_id = clip->events[i].event_id,
                .float_value = clip->events[i].float_value,
                .user_data = clip->events[i].user_data,
                .layer_index = layer_index,
                .time = et,
            }));
        }
    }
}

static i32 mel__mixer_find_output(const Mel_Anim_Mixer* mixer, u64 property_id)
{
    for (usize i = 0; i < mixer->outputs.count; i++)
    {
        if (mixer->outputs.items[i].property_id == property_id)
            return (i32)i;
    }
    return -1;
}

static Mel_Anim_Mixer_Output* mel__mixer_get_or_add_output(Mel_Anim_Mixer* mixer,
                                                            u64 property_id, u32 stride,
                                                            Mel_Blend_Fn blend)
{
    i32 idx = mel__mixer_find_output(mixer, property_id);
    if (idx >= 0)
        return &mixer->outputs.items[idx];

    Mel_Anim_Mixer_Output out = {
        .property_id = property_id,
        .stride = stride,
        .blend = blend,
        .value = {0},
    };
    mel_array_push(&mixer->outputs, out);
    return &mixer->outputs.items[mixer->outputs.count - 1];
}

static void mel__mixer_apply_layer(Mel_Anim_Mixer* mixer,
                                    Mel_Anim_Clip* clip, f32 time, f32 weight,
                                    u32 mix_mode)
{
    f32 buf[4];

    for (u32 t = 0; t < clip->track_count; t++)
    {
        Mel_Anim_Track* track = &clip->tracks[t];
        mel_anim_track_eval(track, time, buf);

        Mel_Anim_Mixer_Output* out = mel__mixer_get_or_add_output(
            mixer, track->property_id, track->stride, track->blend);

        if (mix_mode == MEL_ANIM_MIX_ADD)
        {
            for (u32 c = 0; c < track->stride; c++)
                out->value[c] += buf[c] * weight;
        }
        else if (out->blend)
        {
            f32 tmp[4];
            memcpy(tmp, out->value, sizeof(f32) * track->stride);
            out->blend(out->value, tmp, buf, track->stride, weight);
        }
        else
        {
            for (u32 c = 0; c < track->stride; c++)
                out->value[c] = out->value[c] + (buf[c] - out->value[c]) * weight;
        }
    }
}

static f32 mel__mixer_apply_entry(Mel_Anim_Mixer* mixer,
                                   Mel_Anim_Mix_Entry* entry,
                                   f32 parent_weight, u32 mix_mode)
{
    f32 alpha = (entry->mix_duration > 0.0f)
                    ? entry->mix_elapsed / entry->mix_duration
                    : 1.0f;
    if (alpha > 1.0f) alpha = 1.0f;

    if (entry->from)
    {
        mel__mixer_apply_entry(mixer, entry->from,
                                parent_weight * (1.0f - alpha), mix_mode);
    }

    mel__mixer_apply_layer(mixer, entry->clip, entry->time,
                            parent_weight * alpha, mix_mode);

    return alpha;
}

static void mel__mixer_advance_chain(Mel_Anim_Mix_Entry* entry, f32 dt)
{
    while (entry)
    {
        entry->time += dt * entry->speed;

        if (entry->clip->loop)
        {
            while (entry->time >= entry->clip->duration)
                entry->time -= entry->clip->duration;
        }
        else
        {
            if (entry->time > entry->clip->duration)
                entry->time = entry->clip->duration;
        }

        entry->mix_elapsed += dt;
        entry = entry->from;
    }
}

static void mel__mixer_prune_chain(const Mel_Alloc* alloc, Mel_Anim_Mix_Entry** entry_ptr)
{
    if (!*entry_ptr) return;

    mel__mixer_prune_chain(alloc, &(*entry_ptr)->from);

    if ((*entry_ptr)->mix_elapsed >= (*entry_ptr)->mix_duration)
    {
        mel__mixer_free_chain(alloc, (*entry_ptr)->from);
        (*entry_ptr)->from = NULL;
    }
}

void mel_anim_mixer_update(Mel_Anim_Mixer* mixer, f32 dt)
{
    assert(mixer != NULL);

    mixer->outputs.count = 0;

    for (usize i = 0; i < mixer->defaults.count; i++)
    {
        Mel_Anim_Mixer_Default* def = &mixer->defaults.items[i];
        Mel_Anim_Mixer_Output out = {
            .property_id = def->property_id,
            .stride = def->stride,
            .blend = def->blend,
            .value = {0},
        };
        memcpy(out.value, def->value, sizeof(f32) * def->stride);
        mel_array_push(&mixer->outputs, out);
    }

    mel_array_clear(&mixer->pending_events);

    for (usize li = 0; li < mixer->layers.count; li++)
    {
        Mel_Anim_Layer* l = &mixer->layers.items[li];

        if (!l->clip || !l->playing)
            continue;

        f32 prev_time = l->time;
        l->time += dt * l->speed;

        if (l->clip->loop)
        {
            while (l->time >= l->clip->duration)
            {
                mel__mixer_fire_lifecycle(mixer, (u32)li, MEL_ANIM_EVENT_LOOPED, l->time);
                l->time -= l->clip->duration;
            }
        }
        else
        {
            if (l->time >= l->clip->duration)
            {
                l->time = l->clip->duration;
                if (!l->finished)
                {
                    l->finished = true;
                    mel__mixer_fire_lifecycle(mixer, (u32)li, MEL_ANIM_EVENT_COMPLETED, l->time);

                    if (l->queue.count > 0)
                    {
                        Mel_Anim_Queue_Entry qe = l->queue.items[0];
                        mel_array_remove_ordered(&l->queue, 0);

                        mel__mixer_free_chain(mixer->alloc, l->mix_from);
                        l->mix_from = NULL;

                        if (qe.mix_duration > 0.0f)
                        {
                            Mel_Anim_Mix_Entry* entry = mel_alloc_type(mixer->alloc, Mel_Anim_Mix_Entry);
                            entry->clip = l->clip;
                            entry->time = l->time;
                            entry->speed = l->speed;
                            entry->mix_duration = 0.0f;
                            entry->mix_elapsed = 0.0f;
                            entry->from = NULL;

                            l->mix_from = entry;
                            l->mix_duration = qe.mix_duration;
                            l->mix_elapsed = 0.0f;
                        }

                        l->clip = qe.clip;
                        l->time = 0.0f;
                        l->finished = false;
                    
                        mel__mixer_fire_lifecycle(mixer, (u32)li, MEL_ANIM_EVENT_STARTED, 0.0f);
                    }
                }
            }
        }

        mel__mixer_collect_events(mixer, l->clip, prev_time, l->time, (u32)li);

        if (l->mix_from && l->mix_duration > 0.0f)
        {
            l->mix_elapsed += dt;
            f32 alpha = l->mix_elapsed / l->mix_duration;
            bool crossfade_done = false;
            if (alpha >= 1.0f)
            {
                alpha = 1.0f;
                crossfade_done = true;
            }

            if (!crossfade_done)
            {
                mel__mixer_advance_chain(l->mix_from, dt);

                f32 old_weight = l->weight * (1.0f - alpha);
                mel__mixer_apply_entry(mixer, l->mix_from, old_weight, l->mix_mode);

                f32 new_weight = l->weight * alpha;
                mel__mixer_apply_layer(mixer, l->clip, l->time, new_weight, l->mix_mode);
            }
            else
            {
                mel__mixer_free_chain(mixer->alloc, l->mix_from);
                l->mix_from = NULL;
                mel__mixer_apply_layer(mixer, l->clip, l->time, l->weight, l->mix_mode);
            }

            mel__mixer_prune_chain(mixer->alloc, &l->mix_from);
        }
        else
        {
            mel__mixer_apply_layer(mixer, l->clip, l->time, l->weight, l->mix_mode);
        }
    }
}

u32 mel_anim_mixer_output_count(const Mel_Anim_Mixer* mixer)
{
    assert(mixer != NULL);
    return (u32)mixer->outputs.count;
}

const Mel_Anim_Mixer_Output* mel_anim_mixer_output(const Mel_Anim_Mixer* mixer, u32 index)
{
    assert(mixer != NULL);
    assert(index < mixer->outputs.count);
    return &mixer->outputs.items[index];
}

const Mel_Anim_Mixer_Output* mel_anim_mixer_find_output(const Mel_Anim_Mixer* mixer, u64 property_id)
{
    assert(mixer != NULL);

    for (usize i = 0; i < mixer->outputs.count; i++)
    {
        if (mixer->outputs.items[i].property_id == property_id)
            return &mixer->outputs.items[i];
    }

    return NULL;
}

void mel_anim_mixer_flush_events(Mel_Anim_Mixer* mixer)
{
    assert(mixer != NULL);

    for (usize i = 0; i < mixer->pending_events.count; i++)
        mel_event_channel_fire(&mixer->events, &mixer->pending_events.items[i]);

    mel_array_clear(&mixer->pending_events);
}
