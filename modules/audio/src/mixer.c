#include "audio_internal.h"

#include <core/types.h>
#include <collection/array.h>
#include <collection/slotmap.fwd.h>
#include <math/scalar.h>

#include <string.h>

static void mel_audio__voice_apply_fades(Mel_Audio* eng, Mel_Audio__Voice* v)
{
    f64 clock = eng->stream_clock;

    if (v->fade_volume.active)
    {
        u32 done = 0;
        f32 val = mel_audio__fade_eval(&v->fade_volume, clock, &done);
        v->volume = val;
        if (done)
        {
            if (v->fade_volume.on_complete_pause)
                v->flags |= MEL_AUDIO_VOICE_PAUSED;
            if (v->fade_volume.on_complete_stop)
                v->flags |= MEL_AUDIO__VOICE_ENDED;
            v->fade_volume.active = 0u;
        }
    }
    if (v->fade_pan.active)
    {
        u32 done = 0;
        v->pan = mel_audio__fade_eval(&v->fade_pan, clock, &done);
        mel_audio__pan_gains(v->pan, &v->gain_l, &v->gain_r);
        if (done)
            v->fade_pan.active = 0u;
    }
    if (v->fade_speed.active)
    {
        u32 done = 0;
        v->play_speed = (f64)mel_audio__fade_eval(&v->fade_speed, clock, &done);
        if (done)
            v->fade_speed.active = 0u;
    }
}

u32 mel_audio__mix_block(Mel_Audio* eng, f32* planar_out, u32 frames)
{
    assert(eng != NULL);
    assert(planar_out != NULL);

    u32 out_channels = eng->caps.channels;
    f64 dst_rate = (f64)eng->caps.samplerate;

    memset(planar_out, 0, sizeof(f32) * (usize)out_channels * (usize)frames);

    u32 master_done = 0;
    if (eng->master_fade.active)
    {
        eng->master_volume = mel_audio__fade_eval(&eng->master_fade, eng->stream_clock, &master_done);
        if (master_done)
            eng->master_fade.active = 0u;
    }

    u32 ended_count = 0;
    u32 packed = (u32)eng->voices.packed.count;

    for (u32 vi = 0; vi < packed; vi++)
    {
        Mel_Audio__Voice* v = &eng->voices.packed.items[vi];

        if (v->source == NULL)
            continue;

        mel_audio__voice_apply_fades(eng, v);

        if (v->flags & MEL_AUDIO__VOICE_ENDED)
        {
            ended_count++;
            continue;
        }

        if (v->flags & MEL_AUDIO_VOICE_PAUSED)
            continue;

        u32 src_channels = v->source->channels;
        f64 ratio = (v->source->base_samplerate / dst_rate) * v->play_speed;
        assert(ratio > 0.0);

        f64 cursor_frac = v->cursor;
        u32 base = v->has_tail ? 1u : 0u;
        u32 advance = (u32)(cursor_frac + (f64)frames * ratio);
        u32 fresh = advance + 1u > base ? advance + 1u - base : 0u;
        u32 wb_frames = base + fresh;

        assert(src_channels <= eng->scratch_channels);
        assert(wb_frames <= eng->scratch_fetch_frames);

        memset(eng->scratch_voice, 0, sizeof(f32) * (usize)src_channels * (usize)wb_frames);
        for (u32 c = 0; c < src_channels; c++)
        {
            if (base)
                eng->scratch_voice[(usize)c * wb_frames] = v->tail[c];
        }

        u32 got_fresh = 0u;
        if (fresh > 0u)
            got_fresh = v->source->get_audio(v->source, v->instance, eng->scratch_voice + (usize)base, fresh);
        u32 wb_got = base + got_fresh;

        u32 valid_out = frames;
        u32 ended = 0;
        if (got_fresh < fresh && !(v->flags & MEL_AUDIO_VOICE_LOOPING))
        {
            if (wb_got == 0u)
            {
                valid_out = 0u;
            }
            else
            {
                f64 reach = ((f64)(wb_got - 1u) - cursor_frac) / ratio;
                u32 vo = reach > 0.0 ? (u32)reach + 1u : 0u;
                valid_out = vo < frames ? vo : frames;
            }
            ended = 1u;
        }

        f32 vol = v->volume;
        for (u32 c = 0; c < src_channels; c++)
        {
            const f32* src_plane = eng->scratch_voice + (usize)c * wb_frames;
            f32*       res_plane = eng->scratch_resampled + (usize)c * frames;
            memset(res_plane, 0, sizeof(f32) * (usize)frames);
            f64 cursor = cursor_frac;
            if (valid_out > 0u)
                eng->resampler(src_plane, wb_got, res_plane, valid_out, ratio, &cursor);
            if (vol != 1.0f)
            {
                for (u32 i = 0; i < valid_out; i++)
                    res_plane[i] *= vol;
            }
        }

        if (!ended)
        {
            for (u32 c = 0; c < src_channels; c++)
                v->tail[c] = eng->scratch_voice[(usize)c * wb_frames + advance];
            v->has_tail = 1u;
            v->cursor = (cursor_frac + (f64)frames * ratio) - (f64)advance;
        }

        mel_audio__pan_accumulate(eng->scratch_resampled, src_channels, frames, v->gain_l, v->gain_r, planar_out, out_channels);

        if (ended)
        {
            v->flags |= MEL_AUDIO__VOICE_ENDED;
            ended_count++;
        }
    }

    for (;;)
    {
        Mel_SlotMap_Handle victim = MEL_SLOTMAP_HANDLE_NULL;
        for (u32 i = 0; i < (u32)eng->voices.packed.count; i++)
        {
            if (eng->voices.packed.items[i].flags & MEL_AUDIO__VOICE_ENDED)
            {
                victim = eng->voices.packed.items[i].self;
                break;
            }
        }
        if (!mel_slotmap_handle_valid(victim))
            break;
        mel_audio__voice_remove(eng, victim);
    }

    f32 master = eng->master_volume;
    if (master != 1.0f)
    {
        usize total = (usize)out_channels * (usize)frames;
        for (usize i = 0; i < total; i++)
            planar_out[i] *= master;
    }

    eng->stream_clock += (f64)frames;

    return ended_count;
}
