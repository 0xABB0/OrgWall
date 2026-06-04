#pragma once

#include <core/types.h>
#include <time/duration.h>
#include <time/nano.h>

#ifdef __cplusplus
extern "C"
{
#endif

Mel_Duration mel_sleep(Mel_Duration d);
Mel_Duration mel_sleep_until(mel_nanosec deadline);
Mel_Duration mel_busy_wait(Mel_Duration d);
Mel_Duration mel_busy_wait_until(mel_nanosec deadline);

static inline Mel_Duration mel_sleep_ns(u64 ns) { return mel_sleep(mel_dur_ns((i64)ns)); }
static inline Mel_Duration mel_sleep_ms(u64 ms) { return mel_sleep(mel_dur_ms((i64)ms)); }

#ifdef __cplusplus
}
#endif
