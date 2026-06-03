#include <audio/engine.h>

#include "audio_internal.h"

#include <core/types.h>

u32 mel_audio_render(Mel_Audio* eng, f32* interleaved_dst, u32 frames)
{
    assert(eng != NULL);
    assert(interleaved_dst != NULL);
    assert(eng->online == 0u);

    if (frames == 0u)
        return 0u;

    mel_audio__commands_drain(eng);

    u32 out_channels = eng->caps.channels;

    mel_audio__scratch_ensure_offline(eng, frames);

    mel_audio__mix_block(eng, eng->scratch_planar, frames);

    for (u32 c = 0; c < out_channels; c++)
    {
        const f32* plane = eng->scratch_planar + (usize)c * frames;
        for (u32 i = 0; i < frames; i++)
            interleaved_dst[(usize)i * out_channels + c] = plane[i];
    }

    return frames;
}
