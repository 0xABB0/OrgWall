# time

Time sources, duration vocabulary, frame pacing, civil-calendar conversion, blocking delays, and host date/time-format preference. Depends on `core` and `string` (allocator-returning formatters and `str8` parse follow the engine string idiom); the format-preference registry additionally pulls `allocator`, `collection`, `debug`.

## Surface

`nano.h` — `mel_nanos_since_unspecified_epoch()`: monotonic `u64` ns (CLOCK_MONOTONIC / QPC). The only absolute reading; unsigned, non-decreasing.

`duration.h` — `Mel_Duration` (`i64` ns, signed). Constructors `mel_dur_{ns,us,ms,secs,mins,secs_f64}`, extractors `mel_dur_{to_ns,to_us,to_ms,as_secs_f64,as_ms_f64}`, saturating `mel_dur_{add,sub,scale}`, `mel_dur_{cmp,min,max,abs}`. Formatting in largest-fitting unit: `mel_dur_str(alloc, d)` → `str8`; `mel_dur_format(d, buf, cap)` is the no-alloc primitive (parity with `str8_to_buf`).

`stopwatch.h` — `Mel_Stopwatch`: `start`/`elapsed`/`restart`. `_at(now)` variants drive it from any source.

`frame_clock.h` — `Mel_Frame_Clock`: per-frame `tick` → clamped delta. `max_dt<=0` disables the spiral-of-death clamp; `smoothing` in `[0,1]` is the weight of history (`0` = pass-through). `_at(now)` variants for deterministic use.

`clock.h` — wall time and calendar. `mel_wall_now_ns()` (CLOCK_REALTIME / GetSystemTimePreciseAsFileTime). `Mel_Civil` (integer fields, ISO weekday 1..7, `tz_offset_min` east of UTC). `mel_civil_{from_unix_ns,to_unix_ns}` via branchless civil↔days. `mel_civil_iso8601(alloc, c)` → `str8` and `mel_civil_parse_iso8601(str8, out)`; `mel_civil_format_iso8601(c, buf, cap)` is the no-alloc primitive. `Mel_Clock_Anchor` + `mel_wall_from_mono` map a stored monotonic stamp to wall time (drifts; re-anchor to correct).

`sleep.h` — synchronous, thread-blocking delays. `mel_sleep(d)` blocks the calling thread for at least `d` (EINTR-restarted on POSIX, high-resolution waitable timer on win32) and returns the `Mel_Duration` actually elapsed against the monotonic clock; `mel_sleep_ns`/`mel_sleep_ms` are convenience wrappers; `mel_sleep_until(deadline)` sleeps to a monotonic deadline. `mel_busy_wait(d)` / `mel_busy_wait_until(deadline)` spin against the monotonic clock with a per-arch relax hint, for sub-scheduler-granularity precision; they burn CPU (MEL-ENGINE-III — use only when the deadline is too tight for the scheduler). Non-positive durations and past deadlines return `0` immediately.

`format_prefs.h` / `format_provider.h` — host regional preference for date ordering (`MEL_DATE_ORDER_{YMD,DMY,MDY}`) and clock style (`MEL_CLOCK_{24H,12H}`), modeled as flag bitsets (no closed-set enums; one bit is set in a resolved snapshot). `mel_time_format_init(alloc)` registers the host provider and probes once; `mel_time_format_refresh()` re-probes; `mel_time_format_prefs()` returns `{ value, status }`, status being severity (`Ok | Warned | Error`) plus flags (`Unavailable`, `Order_Guessed`, `Clock_Guessed`) — never an error string. `mel_time_format_date(prefs, y, m, d, buf, cap)` renders a date in the resolved order using the resolved separator. Backends: apple `NSDateFormatter` templates, linux `nl_langinfo(D_FMT/T_FMT)`, win32 `GetLocaleInfoEx(LOCALE_SSHORTDATE/LOCALE_ITIME)`; android/wasm register no provider yet, so prefs report `Error | Unavailable` honestly (MEL-ENGINE-VIII). A test injects a fake provider via `src/format_backend.h`. Providers register through the same descriptor spine `locale`/`vibration` use; `mel_time_format__set_host_provider_override` swaps the host provider for tests.

## Conventions

Monotonic for elapsed/scheduling/sleeping; wall only for display and persistence. Leap seconds unsupported (Unix-time convention). ISO parse requires an explicit zone (`Z` or `±hh:mm`) — no silent local default. Date-format preference has no silent fallback: an unresolved provider yields `Error | Unavailable`, and `mel_time_format_date` asserts a singular order rather than guessing.

## Not here

`timer.h` declares an unimplemented `mel_schedule`; real event-loop timers live in `reactor` (`mel_reactor_timer_new`). `sleep.h` is the synchronous counterpart and deliberately does not overlap it: `mel_sleep` blocks one thread, never touches the reactor/proactor, fires no callback, and schedules nothing — use the reactor for non-blocking, callback-driven, cancellable timers, and `sleep.h` only when blocking the current thread is intended.
