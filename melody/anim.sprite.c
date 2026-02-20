#include "anim.sprite.h"
#include "anim.mixer.h"
#include "anim.clip.h"
#include "anim.track.h"
#include "math.curve.h"
#include "allocator.h"

void mel_anim_sprite_init(Mel_Anim_Sprite* sprite, const Mel_Alloc* alloc)
{
    assert(sprite != NULL);
    assert(alloc != NULL);

    mel_anim_mixer_init(&sprite->mixer, alloc);
    mel_anim_mixer_add_layer(&sprite->mixer);
}

void mel_anim_sprite_destroy(Mel_Anim_Sprite* sprite)
{
    assert(sprite != NULL);
    mel_anim_mixer_destroy(&sprite->mixer);
}

void mel_anim_sprite_play(Mel_Anim_Sprite* sprite, Mel_Anim_Clip* clip,
                          f32 mix_duration)
{
    assert(sprite != NULL);
    assert(clip != NULL);
    mel_anim_mixer_play(&sprite->mixer, 0, clip, mix_duration);
}

void mel_anim_sprite_update(Mel_Anim_Sprite* sprite, f32 dt)
{
    assert(sprite != NULL);
    mel_anim_mixer_update(&sprite->mixer, dt);
}

u32 mel_anim_sprite_frame_index(const Mel_Anim_Sprite* sprite)
{
    assert(sprite != NULL);

    const Mel_Anim_Mixer_Output* out =
        mel_anim_mixer_find_output(&sprite->mixer, MEL_ANIM_PROP_SPRITE_FRAME);

    if (!out)
        return 0;

    return (u32)out->value[0];
}

Mel_Anim_Clip mel_anim_sprite_clip(const Mel_Alloc* alloc,
                                    u64 name_hash,
                                    const u32* frame_indices,
                                    const f32* frame_durations,
                                    u32 frame_count, bool loop)
{
    assert(alloc != NULL);
    assert(frame_indices != NULL);
    assert(frame_durations != NULL);
    assert(frame_count > 0);

    f32 total_duration = 0.0f;
    for (u32 i = 0; i < frame_count; i++)
        total_duration += frame_durations[i];

    Mel_Anim_Clip clip;
    mel_anim_clip_init(&clip, alloc, name_hash, 1, 0, total_duration, loop);

    Mel_Anim_Track* track = mel_anim_clip_track(&clip, 0);
    mel_anim_track_init(track, alloc, MEL_ANIM_PROP_SPRITE_FRAME,
                        MEL_ANIM_TRACK_F32, frame_count);

    f32 time = 0.0f;
    for (u32 i = 0; i < frame_count; i++)
    {
        f32 val = (f32)frame_indices[i];
        mel_anim_track_set_keyframe(track, i, time, &val, MEL_CURVE_STEPPED);
        time += frame_durations[i];
    }

    return clip;
}
