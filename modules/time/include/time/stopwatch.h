#pragma once

#include <time/duration.h>
#include <time/nano.h>

typedef struct
{
    mel_nanosec origin;
} Mel_Stopwatch;

static inline Mel_Duration mel_stopwatch_elapsed_at(const Mel_Stopwatch* sw, mel_nanosec now) { return (Mel_Duration)(now - sw->origin); }

static inline Mel_Duration mel_stopwatch_restart_at(Mel_Stopwatch* sw, mel_nanosec now)
{
    Mel_Duration e = (Mel_Duration)(now - sw->origin);
    sw->origin = now;
    return e;
}

static inline void         mel_stopwatch_start(Mel_Stopwatch* sw) { sw->origin = mel_nanos_since_unspecified_epoch(); }
static inline Mel_Duration mel_stopwatch_elapsed(const Mel_Stopwatch* sw) { return mel_stopwatch_elapsed_at(sw, mel_nanos_since_unspecified_epoch()); }
static inline Mel_Duration mel_stopwatch_restart(Mel_Stopwatch* sw) { return mel_stopwatch_restart_at(sw, mel_nanos_since_unspecified_epoch()); }
