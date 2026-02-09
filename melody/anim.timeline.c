#include "anim.timeline.h"

void mel_anim_playback_start(Mel_Anim_Playback* pb, Mel_Anim_Timeline* timeline)
{
    assert(pb != nullptr);
    assert(timeline != nullptr);
    assert(timeline->keyframe_count > 0);

    pb->timeline = timeline;
    pb->current_keyframe = 0;
    pb->timer = 0.0f;
    pb->playing = true;
    pb->finished = false;
}

void mel_anim_playback_update(Mel_Anim_Playback* pb, f32 dt)
{
    assert(pb != nullptr);

    if (!pb->playing || pb->finished || !pb->timeline)
        return;

    Mel_Anim_Timeline* tl = pb->timeline;
    pb->timer += dt;

    f32 frame_dur = tl->keyframes[pb->current_keyframe].duration;

    while (pb->timer >= frame_dur)
    {
        pb->timer -= frame_dur;
        pb->current_keyframe++;

        if (pb->current_keyframe >= tl->keyframe_count)
        {
            if (tl->loop)
            {
                pb->current_keyframe = 0;
            }
            else
            {
                pb->current_keyframe = tl->keyframe_count - 1;
                pb->playing = false;
                pb->finished = true;
                pb->timer = 0.0f;
                return;
            }
        }

        frame_dur = tl->keyframes[pb->current_keyframe].duration;
        if (frame_dur <= 0.0f)
            break;
    }
}

void mel_anim_playback_stop(Mel_Anim_Playback* pb)
{
    assert(pb != nullptr);
    pb->playing = false;
}

Mel_Anim_Event* mel_anim_playback_event(Mel_Anim_Playback* pb)
{
    assert(pb != nullptr);

    if (!pb->timeline || !pb->timeline->events)
        return nullptr;

    Mel_Anim_Event* ev = &pb->timeline->events[pb->current_keyframe];
    if (ev->flags == 0)
        return nullptr;

    return ev;
}

u32 mel_anim_playback_frame_index(Mel_Anim_Playback* pb)
{
    assert(pb != nullptr);

    if (!pb->timeline || pb->timeline->keyframe_count == 0)
        return 0;

    return pb->timeline->keyframes[pb->current_keyframe].frame_index;
}
