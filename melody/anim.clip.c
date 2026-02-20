#include "anim.clip.h"
#include "anim.track.h"
#include "allocator.h"

void mel_anim_clip_init(Mel_Anim_Clip* clip, const Mel_Alloc* alloc,
                        u64 name_hash, u32 track_count, u32 event_count,
                        f32 duration, bool loop)
{
    assert(clip != NULL);
    assert(alloc != NULL);

    clip->name_hash = name_hash;
    clip->track_count = track_count;
    clip->event_count = event_count;
    clip->duration = duration;
    clip->loop = loop;

    clip->tracks = track_count > 0
                   ? mel_alloc_array(alloc, Mel_Anim_Track, track_count)
                   : NULL;
    clip->events = event_count > 0
                   ? mel_alloc_array(alloc, Mel_Anim_Clip_Event, event_count)
                   : NULL;
}

void mel_anim_clip_destroy(Mel_Anim_Clip* clip, const Mel_Alloc* alloc)
{
    assert(clip != NULL);
    assert(alloc != NULL);

    for (u32 i = 0; i < clip->track_count; i++)
        mel_anim_track_destroy(&clip->tracks[i], alloc);

    if (clip->tracks)
        mel_dealloc(alloc, clip->tracks);
    if (clip->events)
        mel_dealloc(alloc, clip->events);

    *clip = (Mel_Anim_Clip){0};
}

Mel_Anim_Track* mel_anim_clip_track(Mel_Anim_Clip* clip, u32 index)
{
    assert(clip != NULL);
    assert(index < clip->track_count);
    return &clip->tracks[index];
}

Mel_Anim_Track* mel_anim_clip_find_track(Mel_Anim_Clip* clip, u64 property_id)
{
    assert(clip != NULL);

    for (u32 i = 0; i < clip->track_count; i++)
    {
        if (clip->tracks[i].property_id == property_id)
            return &clip->tracks[i];
    }

    return NULL;
}

bool mel_anim_clip_has_property(const Mel_Anim_Clip* clip, u64 property_id)
{
    assert(clip != NULL);

    for (u32 i = 0; i < clip->track_count; i++)
    {
        if (clip->tracks[i].property_id == property_id)
            return true;
    }

    return false;
}
