# time

Time sources, duration vocabulary, frame pacing, and civil-calendar conversion. Depends on `core` and `string` (allocator-returning formatters and `str8` parse follow the engine string idiom).

## Surface

`nano.h` — `mel_nanos_since_unspecified_epoch()`: monotonic `u64` ns (CLOCK_MONOTONIC / QPC). The only absolute reading; unsigned, non-decreasing.

`duration.h` — `Mel_Duration` (`i64` ns, signed). Constructors `mel_dur_{ns,us,ms,secs,mins,secs_f64}`, extractors `mel_dur_{to_ns,to_us,to_ms,as_secs_f64,as_ms_f64}`, saturating `mel_dur_{add,sub,scale}`, `mel_dur_{cmp,min,max,abs}`. Formatting in largest-fitting unit: `mel_dur_str(alloc, d)` → `str8`; `mel_dur_format(d, buf, cap)` is the no-alloc primitive (parity with `str8_to_buf`).

`stopwatch.h` — `Mel_Stopwatch`: `start`/`elapsed`/`restart`. `_at(now)` variants drive it from any source.

`frame_clock.h` — `Mel_Frame_Clock`: per-frame `tick` → clamped delta. `max_dt<=0` disables the spiral-of-death clamp; `smoothing` in `[0,1]` is the weight of history (`0` = pass-through). `_at(now)` variants for deterministic use.

`clock.h` — wall time and calendar. `mel_wall_now_ns()` (CLOCK_REALTIME / GetSystemTimePreciseAsFileTime). `Mel_Civil` (integer fields, ISO weekday 1..7, `tz_offset_min` east of UTC). `mel_civil_{from_unix_ns,to_unix_ns}` via branchless civil↔days. `mel_civil_iso8601(alloc, c)` → `str8` and `mel_civil_parse_iso8601(str8, out)`; `mel_civil_format_iso8601(c, buf, cap)` is the no-alloc primitive. `Mel_Clock_Anchor` + `mel_wall_from_mono` map a stored monotonic stamp to wall time (drifts; re-anchor to correct).

## Conventions

Monotonic for elapsed/scheduling; wall only for display and persistence. Leap seconds unsupported (Unix-time convention). ISO parse requires an explicit zone (`Z` or `±hh:mm`) — no silent local default.

## Not here

`timer.h` declares an unimplemented `mel_schedule`; real event-loop timers live in `reactor` (`mel_reactor_timer_new`). Sleep/yield/deadline primitives are unprovided.
