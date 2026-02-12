#include "anim.blend.h"
#include "anim.timeline.h"

u32 mel_anim_blend_frame_index(Mel_Anim_Blend* blend)
{
    assert(blend != nullptr);
    assert(blend->layer_count > 0);

    u32 best = 0;
    f32 best_weight = blend->weights[0];

    for (u32 i = 1; i < blend->layer_count; i++)
    {
        if (blend->weights[i] > best_weight)
        {
            best_weight = blend->weights[i];
            best = i;
        }
    }

    return mel_anim_playback_frame_index(&blend->layers[best]);
}
