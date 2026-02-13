#pragma once

#include "core.types.h"
#include "anim.blend.fwd.h"
#include "anim.timeline.fwd.h"

struct Mel_Anim_Blend {
    Mel_Anim_Playback* layers;
    f32* weights;
    u32 layer_count;
};

u32 mel_anim_blend_frame_index(Mel_Anim_Blend* blend);
