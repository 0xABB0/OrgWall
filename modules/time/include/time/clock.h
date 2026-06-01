#pragma once

#include <allocator/allocator.fwd.h>
#include <core/types.h>
#include <string/str8.fwd.h>
#include <time/nano.h>

mel_nanosec mel_wall_now_ns(void);

typedef struct
{
    i32 year;
    u8  month;
    u8  day;
    u8  hour;
    u8  minute;
    u8  second;
    u32 nanosecond;
    u8  weekday;
    i16 tz_offset_min;
} Mel_Civil;

Mel_Civil   mel_civil_from_unix_ns(mel_nanosec utc, i16 tz_offset_min);
mel_nanosec mel_civil_to_unix_ns(Mel_Civil c);

usize mel_civil_format_iso8601(Mel_Civil c, char* out, usize cap);
str8  mel_civil_iso8601(const Mel_Alloc* alloc, Mel_Civil c);
bool  mel_civil_parse_iso8601(str8 in, Mel_Civil* out);

typedef struct
{
    mel_nanosec mono;
    mel_nanosec wall;
} Mel_Clock_Anchor;

void        mel_clock_anchor_now(Mel_Clock_Anchor* a);
mel_nanosec mel_wall_from_mono(const Mel_Clock_Anchor* a, mel_nanosec mono);
