#pragma once

#include <allocator/allocator.fwd.h>
#include <core/types.h>
#include <string/str8.fwd.h>

typedef i64 Mel_Duration;

static constexpr i64 MEL_NANOS_PER_US  = 1000;
static constexpr i64 MEL_NANOS_PER_MS  = 1000 * 1000;
static constexpr i64 MEL_NANOS_PER_SEC = 1000 * 1000 * 1000;
static constexpr i64 MEL_NANOS_PER_MIN = 60LL * 1000 * 1000 * 1000;

static constexpr Mel_Duration MEL_DURATION_MAX = INT64_MAX;
static constexpr Mel_Duration MEL_DURATION_MIN = INT64_MIN;

static inline Mel_Duration mel__sat_add(i64 a, i64 b)
{
    i64 r;
    if (__builtin_add_overflow(a, b, &r))
        return b > 0 ? MEL_DURATION_MAX : MEL_DURATION_MIN;
    return r;
}

static inline Mel_Duration mel__sat_sub(i64 a, i64 b)
{
    i64 r;
    if (__builtin_sub_overflow(a, b, &r))
        return b < 0 ? MEL_DURATION_MAX : MEL_DURATION_MIN;
    return r;
}

static inline Mel_Duration mel__sat_mul(i64 a, i64 b)
{
    i64 r;
    if (__builtin_mul_overflow(a, b, &r))
        return (a > 0) == (b > 0) ? MEL_DURATION_MAX : MEL_DURATION_MIN;
    return r;
}

static inline Mel_Duration mel_dur_ns(i64 ns) { return ns; }
static inline Mel_Duration mel_dur_us(i64 us) { return mel__sat_mul(us, MEL_NANOS_PER_US); }
static inline Mel_Duration mel_dur_ms(i64 ms) { return mel__sat_mul(ms, MEL_NANOS_PER_MS); }
static inline Mel_Duration mel_dur_secs(i64 s) { return mel__sat_mul(s, MEL_NANOS_PER_SEC); }
static inline Mel_Duration mel_dur_mins(i64 m) { return mel__sat_mul(m, MEL_NANOS_PER_MIN); }

static inline Mel_Duration mel_dur_secs_f64(f64 s)
{
    assert(s == s);
    f64 ns = s * (f64)MEL_NANOS_PER_SEC;
    if (ns >= (f64)MEL_DURATION_MAX)
        return MEL_DURATION_MAX;
    if (ns <= (f64)MEL_DURATION_MIN)
        return MEL_DURATION_MIN;
    return (Mel_Duration)ns;
}

static inline i64 mel_dur_to_ns(Mel_Duration d) { return d; }
static inline i64 mel_dur_to_us(Mel_Duration d) { return d / MEL_NANOS_PER_US; }
static inline i64 mel_dur_to_ms(Mel_Duration d) { return d / MEL_NANOS_PER_MS; }
static inline f64 mel_dur_as_secs_f64(Mel_Duration d) { return (f64)d / (f64)MEL_NANOS_PER_SEC; }
static inline f64 mel_dur_as_ms_f64(Mel_Duration d) { return (f64)d / (f64)MEL_NANOS_PER_MS; }

static inline Mel_Duration mel_dur_add(Mel_Duration a, Mel_Duration b) { return mel__sat_add(a, b); }
static inline Mel_Duration mel_dur_sub(Mel_Duration a, Mel_Duration b) { return mel__sat_sub(a, b); }
static inline Mel_Duration mel_dur_scale(Mel_Duration d, i64 factor) { return mel__sat_mul(d, factor); }

static inline i32          mel_dur_cmp(Mel_Duration a, Mel_Duration b) { return (a > b) - (a < b); }
static inline Mel_Duration mel_dur_min(Mel_Duration a, Mel_Duration b) { return a < b ? a : b; }
static inline Mel_Duration mel_dur_max(Mel_Duration a, Mel_Duration b) { return a > b ? a : b; }
static inline Mel_Duration mel_dur_abs(Mel_Duration d) { return d < 0 ? mel__sat_sub(0, d) : d; }

usize mel_dur_format(Mel_Duration d, char* out, usize cap);
str8  mel_dur_str(const Mel_Alloc* alloc, Mel_Duration d);
