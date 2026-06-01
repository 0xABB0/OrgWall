# time module augmentation

## Work done

The module previously shipped only `mel_nanos_since_unspecified_epoch()` (monotonic `u64` ns). `clock.h` was empty; `timer.h` declared an unimplemented `mel_schedule`. Three pillars added, each tested, integrated with the engine's `allocator` and `string` (`str8`) vocabulary.

**Duration (`duration.h` + `duration.c`).** `Mel_Duration` = signed `i64` ns (a difference may be negative). Constructors `mel_dur_{ns,us,ms,secs,mins,secs_f64}`, extractors `mel_dur_{to_*,as_*_f64}`, saturating `mel_dur_{add,sub,scale}` via `__builtin_*_overflow`, `mel_dur_{cmp,min,max,abs}`. Formatting: `mel_dur_str(const Mel_Alloc*, Mel_Duration)` → `str8` (measure-then-fill, mirroring `str8_fmt_alloc`); `mel_dur_format(d, buf, cap)` retained as the no-alloc primitive (parity with `str8_to_buf`). `nano.h` dropped `<stdint.h>` for `<core/types.h>` (resolving its TODO) and now includes `duration.h`; `MEL_NANOS_PER_SEC` moved there as `i64`. Why: every consumer was open-coding `MEL_NANOS_PER_SEC` math and diffing raw reads.

**Stopwatch & frame clock (`stopwatch.h`, `frame_clock.h`, header-only).** `Mel_Stopwatch` (start/elapsed/restart). `Mel_Frame_Clock` (per-frame clamped delta, EMA smoothing). Core logic factored into `_at(now)` variants so both are deterministically testable and drivable from any time source (MEL-ENGINE-IX); the clock-reading entry points are thin wrappers. `max_dt<=0` disables the spiral-of-death clamp; `smoothing∈[0,1]` is history weight (`0`=pass-through), asserted. No defaults are silent (MEL-CODE-007).

**Wall-clock & calendar (`clock.h` + `clock.c` + platform `wall_now`).** `mel_wall_now_ns()` (CLOCK_REALTIME / GetSystemTimePreciseAsFileTime). `Mel_Civil` with integer fields (no enums, MEL-CODE-001), ISO weekday 1..7, `tz_offset_min` east of UTC. `mel_civil_{from,to}_unix_ns` via Hinnant's branchless civil↔days — no `gmtime`/`struct tm`/platform `#ifdef`. `mel_civil_iso8601(const Mel_Alloc*, Mel_Civil)` → `str8`, `mel_civil_parse_iso8601(str8, Mel_Civil*)`, plus the no-alloc `mel_civil_format_iso8601` primitive. `Mel_Clock_Anchor` + `mel_wall_from_mono` map a stored monotonic stamp to wall time. Why: there was no wall time anywhere; `log.sink.sqlite.c` hand-rolls `gmtime_r`/`strftime` per platform.

**Allocator/string integration.** `time` now `mel_depends` on `string` (→ `allocator`, `hash`, `collection`). Public headers carry only the fwd headers (`str8.fwd.h`, `allocator.fwd.h`); the full `str8.h`/`allocator.h` stay in the `.c` files. The build's topological-closure include propagation makes `str8` resolvable for `time`'s dependents (gpu, reactor, log) transitively.

Tests: 26 across `time-duration` (8), `time-frame-clock` (7), `time-clock` (11) — saturation edges, backwards-clock guard, smoothing blend, pre-epoch and negative-tz round-trips, ISO format/parse round-trip and malformed rejection — driven through the `str8`/allocator APIs (`mel_alloc_heap`, `MEL_EXPECT_EQ_STR8`). `log`, `reactor`, `gpu` rebuilt clean against the changed `nano.h` and the new transitive `string` dependency. A module `readme.md` was added and `design/time.md` removed per MEL-SPEC-002.

## Kludges (confessing all — MEL-ENGINE-VIII)

- **`frame_clock` vs the planned `frame.pacing`.** `design/frame-pacing.md` plans a `frame.pacing` module owning the per-frame *budget* (`Frame_Info`, mode set, present-timing, vsync wiring). `Mel_Frame_Clock` is a leaf dt utility — no symbol/file collision, different altitude (no swapchain, usable in CLI/sim/test) — but the conceptual overlap is real: an app on `frame.pacing` reads dt from `Frame_Info`'s inter-tick delta, not from `Mel_Frame_Clock`. Surfaced to Gabbo; keep / rename / subsume is his call. I did not consult that doc before implementing — discovered it on cleanup.
- **`mel_wall_now_ns` lives in `nano.{unix,win32}.c`**, files whose name connotes the monotonic source. Kept there to avoid a second platform `#ifdef`; the honest layout is `clock.{unix,win32}.c`.
- **`clock.c` is compiled `ALWAYS` but calls platform-only `mel_wall_now_ns`.** On `wasm` (no nano source selected in `build.c`) `time` won't link — pre-existing for `mel_nanos_since_unspecified_epoch`, now widened to wall time. Not newly broken on built platforms; the wasm gap is untouched.
- **ISO year is `%04d`.** Years <0 or >9999 do not emit the ISO-8601 extended `±YYYYYY` form (a negative year prints `-001-…`). Unhandled.
- **Microseconds format as ASCII `us`, not `µs`** — deliberate (machine-friendly, no UTF-8), deviates from the design prose.
- **`timer.h` left as a dead, unimplemented `mel_schedule` declaration** — out of scope per the selected pillars. A public header promising a symbol that does not exist is itself a MEL-ENGINE-VIII smell.

### Recanted dodge (corrected after Gabbo's review)

The first cut avoided the allocator and `string` to keep `time` a "core-only leaf": formatting returned bytes into a caller buffer and `parse` took `(const char*, usize)`. That was special pleading against MEL-CODE-003 and MEL-ENGINE-IX — the engine's string vocabulary is `str8` and memory comes from an allocator. Corrected: `mel_dur_str` / `mel_civil_iso8601` return `str8` through a `const Mel_Alloc*` (measure-then-fill, no intermediate scratch arrays — which also retired the earlier `frac[16]`/`zone[8]` confession), and `parse` takes `str8`. The no-alloc buffer primitives remain as the lower layer, exactly as `string` keeps `str8_to_buf` beside `str8_fmt_alloc`.
- **`nob` was hand-bootstrapped in the worktree** (`clang -std=c23 -g -Imodules/build -o nob nob.c`) because the binary is not checked in. Expected, not debt.

## CLAUDE.md suggestions (recommendations only)

- The per-module layout the root `CLAUDE.md` describes (`public/ private/ src/ meta/ readme.md spec.md`) does not match reality — modules use `include/<m>/ + src/ + build.c`. Reconcile the doc with the convention, or migrate modules.
- `nano.h`'s `// TODO: use core/types.h` is now resolved; no doc change needed, noted for tracking.

## Suggestions

- **Retire the bespoke wall-clock block** in `log.sink.sqlite.c:216-224` with `mel_civil_format_iso8601(mel_civil_from_unix_ns(mel_wall_now_ns(), 0), …)`. Validates the new API in a live consumer; left undone to keep this change inside the `time` module.
- **Decide `timer.h`'s fate** — delete it (reactor already owns real timers via `mel_reactor_timer_new`) or reduce it to a thin reactor shim.
- **Resolve the `frame_clock` / `frame.pacing` adjacency** before `frame.pacing` is built, so the two do not ship overlapping dt surfaces.
- **Rename `nano.{unix,win32}.c` → `clock.{unix,win32}.c`** (or split wall out) now that they host two distinct sources.
- The declined 4th pillar — **sleep / yield / `Mel_Deadline`** — is the natural next addition when `frame.pacing` or `thread`'s `CLOCK_REALTIME` sem-wait deadlines want a shared primitive.
