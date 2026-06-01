#pragma once

#include <time/duration.h>
#include <time/nano.h>

typedef struct
{
    mel_nanosec  last;
    Mel_Duration max_dt;
    f64          smoothing;
    Mel_Duration raw_dt;
    Mel_Duration smooth_dt;
    u64          frame;
} Mel_Frame_Clock;

static inline void mel_frame_clock_init_at(Mel_Frame_Clock* fc, mel_nanosec now, Mel_Duration max_dt, f64 smoothing)
{
    assert(smoothing >= 0.0 && smoothing <= 1.0);
    fc->last = now;
    fc->max_dt = max_dt;
    fc->smoothing = smoothing;
    fc->raw_dt = 0;
    fc->smooth_dt = 0;
    fc->frame = 0;
}

static inline Mel_Duration mel_frame_clock_tick_at(Mel_Frame_Clock* fc, mel_nanosec now)
{
    Mel_Duration dt = (Mel_Duration)(now - fc->last);
    if (dt < 0)
        dt = 0;
    if (fc->max_dt > 0 && dt > fc->max_dt)
        dt = fc->max_dt;

    fc->last = now;
    fc->raw_dt = dt;
    fc->smooth_dt = fc->frame == 0 ? dt : (Mel_Duration)(fc->smoothing * (f64)fc->smooth_dt + (1.0 - fc->smoothing) * (f64)dt);
    fc->frame++;
    return dt;
}

static inline void         mel_frame_clock_init(Mel_Frame_Clock* fc, Mel_Duration max_dt, f64 smoothing) { mel_frame_clock_init_at(fc, mel_nanos_since_unspecified_epoch(), max_dt, smoothing); }
static inline Mel_Duration mel_frame_clock_tick(Mel_Frame_Clock* fc) { return mel_frame_clock_tick_at(fc, mel_nanos_since_unspecified_epoch()); }
