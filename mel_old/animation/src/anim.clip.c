#include <animation/anim.clip.h>
#include <allocator/allocator.h>

void mel_anim_clip_destroy(Mel_Anim_Clip* clip, const Mel_Alloc* alloc)
{
    assert(clip != NULL);
    assert(alloc != NULL);

    for (u32 g = 0; g < clip->group_count; g++)
    {
        Mel_Track_Group* grp = &clip->groups[g];
        if (grp->property_ids)    mel_dealloc(alloc, grp->property_ids);
        if (grp->keyframe_counts) mel_dealloc(alloc, grp->keyframe_counts);
        if (grp->data_offsets)    mel_dealloc(alloc, grp->data_offsets);
        if (grp->flat_times)      mel_dealloc(alloc, grp->flat_times);
        if (grp->flat_values)     mel_dealloc(alloc, grp->flat_values);
        if (grp->flat_easing_ids) mel_dealloc(alloc, grp->flat_easing_ids);
        if (grp->flat_easing_params) mel_dealloc(alloc, grp->flat_easing_params);
    }

    for (u32 g = 0; g < clip->event_group_count; g++)
    {
        Mel_Event_Group* egrp = &clip->event_groups[g];
        if (egrp->property_ids)      mel_dealloc(alloc, egrp->property_ids);
        if (egrp->keyframe_counts)   mel_dealloc(alloc, egrp->keyframe_counts);
        if (egrp->data_offsets)      mel_dealloc(alloc, egrp->data_offsets);
        if (egrp->flat_times)        mel_dealloc(alloc, egrp->flat_times);
        if (egrp->flat_event_hashes) mel_dealloc(alloc, egrp->flat_event_hashes);
    }

    if (clip->groups)       mel_dealloc(alloc, clip->groups);
    if (clip->event_groups) mel_dealloc(alloc, clip->event_groups);

    *clip = (Mel_Anim_Clip){0};
}

u32 mel_anim_clip_cursor_count(const Mel_Anim_Clip* clip)
{
    assert(clip != NULL);
    u32 total = 0;
    for (u32 g = 0; g < clip->group_count; g++)
        total += clip->groups[g].track_count;
    return total;
}
