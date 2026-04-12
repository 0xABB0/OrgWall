#include "anim.sprite.h"
#include "anim.clip.h"
#include "anim.registry.h"
#include "allocator.h"
#include "math.easing.h"

#include <string.h>
#include <assert.h>

static void mel__sprite_grow_frames(Mel_Sprite_Anim_Def* def)
{
    u32 new_cap = def->frame_capacity == 0 ? 8 : def->frame_capacity * 2;
    usize new_size = sizeof(Mel_Sprite_Anim_Frame) * new_cap;
    if (def->frames == NULL)
        def->frames = mel_alloc(def->alloc, new_size);
    else
        def->frames = mel_realloc(def->alloc, def->frames, new_size);
    def->frame_capacity = new_cap;
}

static void mel__sprite_grow_events(Mel_Sprite_Anim_Def* def)
{
    u32 new_cap = def->event_capacity == 0 ? 4 : def->event_capacity * 2;
    usize new_size = sizeof(Mel_Sprite_Anim_Event) * new_cap;
    if (def->events == NULL)
        def->events = mel_alloc(def->alloc, new_size);
    else
        def->events = mel_realloc(def->alloc, def->events, new_size);
    def->event_capacity = new_cap;
}

void mel_sprite_anim_def_init(Mel_Sprite_Anim_Def* def, u64 name_hash, u64 property_hash, const Mel_Alloc* alloc)
{
    assert(def != NULL);
    assert(alloc != NULL);

    *def = (Mel_Sprite_Anim_Def){0};
    def->name_hash = name_hash;
    def->property_hash = property_hash;
    def->alloc = alloc;
}

void mel_sprite_anim_def_destroy(Mel_Sprite_Anim_Def* def)
{
    assert(def != NULL);

    if (def->frames)
        mel_dealloc(def->alloc, def->frames);
    if (def->events)
        mel_dealloc(def->alloc, def->events);

    *def = (Mel_Sprite_Anim_Def){0};
}

void mel_sprite_anim_def_push_frame(Mel_Sprite_Anim_Def* def, u32 frame_index, f32 duration)
{
    assert(def != NULL);
    assert(duration > 0.0f);

    if (def->frame_count >= def->frame_capacity)
        mel__sprite_grow_frames(def);

    def->frames[def->frame_count++] = (Mel_Sprite_Anim_Frame){
        .frame_index = frame_index,
        .duration = duration,
    };
}

void mel_sprite_anim_def_insert_frame(Mel_Sprite_Anim_Def* def, u32 position,
                                       u32 frame_index, f32 duration)
{
    assert(def != NULL);
    assert(position <= def->frame_count);
    assert(duration > 0.0f);

    if (def->frame_count >= def->frame_capacity)
        mel__sprite_grow_frames(def);

    if (position < def->frame_count)
    {
        memmove(&def->frames[position + 1], &def->frames[position],
                sizeof(Mel_Sprite_Anim_Frame) * (def->frame_count - position));
    }

    def->frames[position] = (Mel_Sprite_Anim_Frame){
        .frame_index = frame_index,
        .duration = duration,
    };
    def->frame_count++;
}

void mel_sprite_anim_def_remove_frame(Mel_Sprite_Anim_Def* def, u32 position)
{
    assert(def != NULL);
    assert(position < def->frame_count);

    if (position < def->frame_count - 1)
    {
        memmove(&def->frames[position], &def->frames[position + 1],
                sizeof(Mel_Sprite_Anim_Frame) * (def->frame_count - position - 1));
    }
    def->frame_count--;
}

void mel_sprite_anim_def_set_frame(Mel_Sprite_Anim_Def* def, u32 position,
                                    u32 frame_index, f32 duration)
{
    assert(def != NULL);
    assert(position < def->frame_count);
    assert(duration > 0.0f);

    def->frames[position].frame_index = frame_index;
    def->frames[position].duration = duration;
}

void mel_sprite_anim_def_add_event(Mel_Sprite_Anim_Def* def, u32 after_frame,
                                    u64 event_hash, u64 event_property)
{
    assert(def != NULL);
    assert(after_frame < def->frame_count);

    if (def->event_count >= def->event_capacity)
        mel__sprite_grow_events(def);

    def->events[def->event_count++] = (Mel_Sprite_Anim_Event){
        .after_frame = after_frame,
        .event_hash = event_hash,
        .event_property = event_property,
    };
}

void mel_sprite_anim_def_remove_event(Mel_Sprite_Anim_Def* def, u32 index)
{
    assert(def != NULL);
    assert(index < def->event_count);

    if (index < def->event_count - 1)
    {
        memmove(&def->events[index], &def->events[index + 1],
                sizeof(Mel_Sprite_Anim_Event) * (def->event_count - index - 1));
    }
    def->event_count--;
}

Mel_Anim_Clip mel_sprite_anim_def_compile(const Mel_Sprite_Anim_Def* def,
                                           const Mel_Alloc* alloc)
{
    assert(def != NULL);
    assert(alloc != NULL);
    assert(def->frame_count > 0);

    u32 kf_count = def->frame_count;
    u32 f32_stride = sizeof(f32);

    f32 total_duration = 0.0f;
    for (u32 i = 0; i < kf_count; i++)
        total_duration += def->frames[i].duration;

    u64 prop_id = def->property_hash;

    Mel_Track_Group* grp = mel_alloc_type(alloc, Mel_Track_Group);
    *grp = (Mel_Track_Group){0};
    grp->type_hash = MEL_ANIM_TYPE_F32;
    grp->track_count = 1;
    grp->property_ids = mel_alloc_type(alloc, u64);
    grp->property_ids[0] = prop_id;
    grp->keyframe_counts = mel_alloc_type(alloc, u32);
    grp->keyframe_counts[0] = kf_count;
    grp->data_offsets = mel_alloc_type(alloc, u32);
    grp->data_offsets[0] = 0;

    grp->flat_times = mel_alloc(alloc, sizeof(f32) * kf_count);
    grp->flat_values = mel_alloc(alloc, (usize)f32_stride * kf_count);
    grp->flat_easing_ids = mel_alloc(alloc, sizeof(u16) * kf_count);

    u16 step_id = MEL_EASING_COUNT - 1;
    f32 cumulative = 0.0f;
    for (u32 i = 0; i < kf_count; i++)
    {
        grp->flat_times[i] = cumulative;
        ((f32*)grp->flat_values)[i] = (f32)def->frames[i].frame_index;
        grp->flat_easing_ids[i] = step_id;
        cumulative += def->frames[i].duration;
    }

    grp->flat_easing_params = NULL;

    u32 event_group_count = 0;
    Mel_Event_Group* event_groups = NULL;

    if (def->event_count > 0)
    {
        u32 unique_props = 0;
        u64* unique_prop_ids = mel_alloc(alloc, sizeof(u64) * def->event_count);
        u32* prop_event_counts = mel_alloc(alloc, sizeof(u32) * def->event_count);

        for (u32 e = 0; e < def->event_count; e++)
        {
            u64 ep = def->events[e].event_property;
            bool found = false;
            for (u32 u = 0; u < unique_props; u++)
            {
                if (unique_prop_ids[u] == ep)
                {
                    prop_event_counts[u]++;
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                unique_prop_ids[unique_props] = ep;
                prop_event_counts[unique_props] = 1;
                unique_props++;
            }
        }

        event_group_count = 1;
        event_groups = mel_alloc_type(alloc, Mel_Event_Group);
        *event_groups = (Mel_Event_Group){0};

        event_groups->track_count = unique_props;
        event_groups->property_ids = mel_alloc(alloc, sizeof(u64) * unique_props);
        event_groups->keyframe_counts = mel_alloc(alloc, sizeof(u32) * unique_props);
        event_groups->data_offsets = mel_alloc(alloc, sizeof(u32) * unique_props);

        memcpy(event_groups->property_ids, unique_prop_ids, sizeof(u64) * unique_props);
        memcpy(event_groups->keyframe_counts, prop_event_counts, sizeof(u32) * unique_props);

        u32 total_events = def->event_count;
        event_groups->flat_times = mel_alloc(alloc, sizeof(f32) * total_events);
        event_groups->flat_event_hashes = mel_alloc(alloc, sizeof(u64) * total_events);

        u32 offset = 0;
        for (u32 t = 0; t < unique_props; t++)
        {
            event_groups->data_offsets[t] = offset;
            u32 written = 0;
            for (u32 e = 0; e < def->event_count; e++)
            {
                if (def->events[e].event_property != unique_prop_ids[t]) continue;

                f32 event_time = 0.0f;
                for (u32 f = 0; f <= def->events[e].after_frame && f < def->frame_count; f++)
                    event_time += def->frames[f].duration;

                event_groups->flat_times[offset + written] = event_time;
                event_groups->flat_event_hashes[offset + written] = def->events[e].event_hash;
                written++;
            }
            offset += prop_event_counts[t];
        }
    }

    Mel_Anim_Clip clip = {
        .name_hash = def->name_hash,
        .duration = total_duration,
        .is_looping = def->is_looping,
        .additive_space = MEL_ANIM_ADDITIVE_LOCAL,
        .groups = grp,
        .group_count = 1,
        .event_groups = event_groups,
        .event_group_count = event_group_count,
    };

    return clip;
}
