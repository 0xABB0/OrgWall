#include <audio/fader.h>

#include "audio_internal.h"

#include <core/types.h>
#include <math/scalar.h>

f32 mel_audio__fade_eval(const Mel_Audio__Scalar_Fade* f, f64 clock, u32* done)
{
    assert(f != NULL);
    assert(done != NULL);

    *done = 0u;

    if (f->kind == MEL_AUDIO__FADER_OSC)
    {
        f64 period = f->period_frames > 0.0 ? f->period_frames : 1.0;
        f64 phase = (clock - f->start_clock) / period;
        f64 t = phase - (f64)(i64)phase;
        if (t < 0.0)
            t += 1.0;
        f64 s = 0.5 - 0.5 * (f64)mel_cosf((f32)(t * (f64)MEL_TAU));
        return (f32)(f->from + (f->to - f->from) * s);
    }

    f64 elapsed = clock - f->start_clock;
    f64 dur = f->duration_frames;
    if (dur <= 0.0 || elapsed >= dur)
    {
        *done = 1u;
        return (f32)f->to;
    }
    if (elapsed < 0.0)
        elapsed = 0.0;

    f64 t = elapsed / dur;
    return (f32)(f->from + (f->to - f->from) * t);
}

void mel_audio_fade_volume(Mel_Audio* eng, Mel_Audio_Voice v, f32 to, f64 seconds)
{
    assert(eng != NULL);
    if (!mel_audio__api_enter(eng))
        return;
    Mel_Audio__Command cmd = {
        .apply = mel_audio__cmd_fade_volume,
        .handle = v.slot,
        .f0 = to,
        .d0 = seconds,
    };
    mel_audio__command_push(eng, &cmd);
    mel_audio__api_leave(eng);
}

void mel_audio_fade_pan(Mel_Audio* eng, Mel_Audio_Voice v, f32 to, f64 seconds)
{
    assert(eng != NULL);
    if (!mel_audio__api_enter(eng))
        return;
    Mel_Audio__Command cmd = {
        .apply = mel_audio__cmd_fade_pan,
        .handle = v.slot,
        .f0 = to,
        .d0 = seconds,
    };
    mel_audio__command_push(eng, &cmd);
    mel_audio__api_leave(eng);
}

void mel_audio_fade_play_speed(Mel_Audio* eng, Mel_Audio_Voice v, f64 to, f64 seconds)
{
    assert(eng != NULL);
    if (!mel_audio__api_enter(eng))
        return;
    Mel_Audio__Command cmd = {
        .apply = mel_audio__cmd_fade_speed,
        .handle = v.slot,
        .d0 = to,
        .d1 = seconds,
    };
    mel_audio__command_push(eng, &cmd);
    mel_audio__api_leave(eng);
}

void mel_audio_oscillate_volume(Mel_Audio* eng, Mel_Audio_Voice v, f32 lo, f32 hi, f64 period)
{
    assert(eng != NULL);
    if (!mel_audio__api_enter(eng))
        return;
    Mel_Audio__Command cmd = {
        .apply = mel_audio__cmd_oscillate_volume,
        .handle = v.slot,
        .f0 = lo,
        .f1 = hi,
        .d0 = period,
    };
    mel_audio__command_push(eng, &cmd);
    mel_audio__api_leave(eng);
}

void mel_audio_schedule_pause(Mel_Audio* eng, Mel_Audio_Voice v, f64 seconds)
{
    assert(eng != NULL);
    if (!mel_audio__api_enter(eng))
        return;
    Mel_Audio__Command cmd = {
        .apply = mel_audio__cmd_schedule_pause,
        .handle = v.slot,
        .d0 = seconds,
    };
    mel_audio__command_push(eng, &cmd);
    mel_audio__api_leave(eng);
}

void mel_audio_schedule_stop(Mel_Audio* eng, Mel_Audio_Voice v, f64 seconds)
{
    assert(eng != NULL);
    if (!mel_audio__api_enter(eng))
        return;
    Mel_Audio__Command cmd = {
        .apply = mel_audio__cmd_schedule_stop,
        .handle = v.slot,
        .d0 = seconds,
    };
    mel_audio__command_push(eng, &cmd);
    mel_audio__api_leave(eng);
}

void mel_audio_fade_master_volume(Mel_Audio* eng, f32 to, f64 seconds)
{
    assert(eng != NULL);
    if (!mel_audio__api_enter(eng))
        return;
    Mel_Audio__Command cmd = {
        .apply = mel_audio__cmd_fade_master,
        .f0 = to,
        .d0 = seconds,
    };
    mel_audio__command_push(eng, &cmd);
    mel_audio__api_leave(eng);
}
