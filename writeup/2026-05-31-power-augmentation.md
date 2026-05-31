# 2026-05-31 — Power module augmentation (profile + battery)

Augmented `modules/power` per `todo.org` line 12: "give information on the power
profile and the battery level." Decisions taken with Gabbo up front — replace the
binary low-power read with a graded profile, represent it as raw-name + coarse
hint (no enum), and carry battery time estimates.

## Work done — what changed, and why

**Public surface (`include/power/power.h`).**
- Added `Mel_Power_Profile { f32 bias; bool present; }` + `mel_power_profile_current`.
  `bias ∈ [-1,+1]` is the OS energy/performance scheme as an *ordinal scalar*, not a
  closed enum — the open, vendor-defined profile set is exactly the case MEL-CODE-001
  warns against, and an ordinal axis is the honest non-enum encoding of an ordered
  category. It also composes forward: when richer surfaces land, the same `f32`
  carries the `+1` end without an API change.
- Added `bool mel_power_profile_name(char* buf, usize cap)` — the verbatim OS token
  beneath the portable bias (MEL-ENGINE-II). Caller-owned buffer, so no allocator is
  threaded and no fixed array is embedded (MEL-CODE-002/003); too-small returns a hard
  `false`, never a silent cut (MEL-ENGINE-VIII).
- `mel_power_low_power_current` is now a *derived* view (`on ⇔ bias < 0`,
  `unknown ⇔ !present`), centralized once in `src/power.c` and composing over every
  platform's profile read (MEL-ENGINE-IX). The per-platform low-power primitives were
  deleted.
- Added `Mel_Power_Battery { present, charging, level, seconds_to_empty,
  seconds_to_full }` + `mel_power_battery_current`. `level ∈ [0,1]`; a negative
  `seconds_*` is the honest absence of that estimate; `present=false` ⇒ ignore the
  rest. This overrides the former §4.3 "charge level deliberately absent" stance, at
  Gabbo's direction (MEL-ENGINE-I — a real product surface the engine should not refuse).
- `Mel_Power_Caps`: `low_power_present` → `profile_present`, added `battery_present`.

**Lowerings (all six platforms, no stubs).**
- `src/power.c` (new, `ALWAYS`) — the shared low-power derivation.
- `src/power_str.h` (new) — `mel_power_name_copy` buffer helper.
- `src/apple/profile.m` (renamed from `lowpower.m`) — macOS+iOS profile via
  `NSProcessInfo.isLowPowerModeEnabled`.
- `src/macos/power.m` — added IOKit IOPS battery (level/charging/time-to-empty/full)
  and caps (caps moved here from the deleted `apple/caps.m`, since `battery_present`
  is per-OS).
- `src/ios/power.m` — added `UIDevice` battery (level/charging) and caps.
- `src/linux/power.c` — profile from `platform_profile` (the one genuinely graded
  source), battery from `/sys/class/power_supply` `capacity`/`status` + energy-or-charge
  time estimate, caps.
- `src/win32/power.c` — profile from the Battery-Saver bit, battery from
  `GetSystemPowerStatus`, caps.
- `src/android/power.c` — profile from `PowerManager.isPowerSaveMode`, battery from the
  `ACTION_BATTERY_CHANGED` intent; refactored the source read to share the intent /
  int-extra JNI helpers.
- `src/web/power.c` — honest absence throughout.
- `build.c` — added `mel_sources(lib, ALWAYS, "src/power.c")`. No new links.

**Verification.**
- Host build clean (`power.o`, `apple/profile.o`, `macos/power.o` → `libpower.a`).
- Cross-compile clean: linux, win32, android, ios, wasm.
- macOS run + observed (M3 Pro / macOS 26.2, plugged in): source `ac`, profile
  `present bias 0 "Automatic"`, low-power `off` (derived), battery `present level 1.000
  not charging, times -1` (full on AC). A self-checking probe asserting the
  derived-low-power and caps-mirror invariants returned `VERDICT PASS` (exit 0).
- Docs updated: `spec.md` (authoritative), `readme.md`, `todo.md`.

## Kludges — every shortcut and the debt it leaves (MEL-ENGINE-VIII)

The bar is zero; these are confessed in full, sanctioned or not.

1. **Saver-only profile on four of five platforms.** Only Linux yields a graded
   `platform_profile`; macOS/iOS/Windows/Android expose just the battery-saver bit, so
   their `bias ∈ {-1, 0}`. This is honest degradation (MEL-ENGINE-VII), documented in
   spec §6.2 with the deferred richer surfaces named — but it means the `+1` end is
   unreachable today on those platforms. Sanctioned (deliberate deferral), not hidden.
2. **Windows overlay scheme deferred.** `PowerGetEffectiveOverlayScheme` would map the
   full range, but I declined the `powrprof` link + header-availability risk against the
   cross toolchain (Windows is unrunnable here per `todo.org`). Pragmatic; debt = a Windows
   profile that under-reports until wired.
3. **Android time estimates absent.** `seconds_to_full` via
   `BatteryManager.computeChargeTimeRemaining()` (API 28+) not implemented; reports `-1`.
   Bounded scope on an untestable-here platform.
4. **Linux battery time estimate assumptions.** I pair `energy_*`/`power_now` fully, else
   `charge_*`/`current_now` fully, to avoid unit-mixing — but unverified on real `/sys`
   layouts. The `balanced-performance → 0.5` bias anchor is an arbitrary judgement call.
5. **Fixed stack scratch buffers in `linux/power.c`** (`char[32]`, `char[256]`,
   `char[300]`). These are transient read scratch, not data-structure arrays, and match
   the existing module idiom (`power_sysfs.h` already does this). I read MEL-CODE-002 as
   targeting expandable data structures, not local scratch — flagging the judgement call.
6. **caps recomputes the battery read.** macOS/iOS `mel_power_caps` calls
   `mel_power_battery_current` to fill `battery_present`, a redundant IOKit/UIDevice query
   at caps time (MEL-ENGINE-III). Acceptable for a startup one-shot; noted.
7. **Verification breadth.** Only macOS was run; the other five are compile/structure-
   verified only. The runtime values are observed on one host, one battery state (full/AC).
8. **Orphan objects on rename.** Deleting `apple/lowpower.m`/`caps.m` left stale `.o`
   files the build system did not auto-prune; I removed them by hand. Minor hygiene gap in
   the build's rename handling.

## CLAUDE.md suggestions (recommendations only — not applied)

- **Clarify the enum policy for telemetry modules.** `power` and `thermal` ship public
  enums (source, low-power, pressure, fidelity) that sit in tension with MEL-CODE-001.
  This augmentation deliberately avoided a new enum (profile is a scalar), but a one-line
  ruling in `coding-guidelines.md` — "OS-reported closed category reads may use enums;
  open/vendor-defined sets must not" — would make the boundary unambiguous for the next
  augmentation rather than re-litigated each time.
- **A shared honest-absence convention.** `thermal` has `Mel_Thermal_Temp_Fidelity`;
  `power` battery uses `present` + negative sentinels; profile uses `present`. A small
  shared `core` convention (or a `Mel_Fidelity`) for "trust this number?" across telemetry
  modules would cut per-module reinvention (MEL-ENGINE-IX at the framework level).

## Suggestions — feature direction & repo hygiene

- **Wire `power-profiles-daemon` (Linux) and the Windows overlay scheme** to close the
  graded-profile gap; both are the documented deferred paths and would make `bias` span
  the full range on the two desktop platforms most likely to be profiled.
- **Land the deferred change-notification contract** (spec §5) once a second consumer
  needs it; the per-platform surfaces are already catalogued.
- **Build hygiene:** the discovery step could prune object files whose source no longer
  exists (the orphan-`.o`-on-rename case above), so a file split/rename leaves a clean
  `build/` without manual `rm`.
- **A `power`/`thermal` example app or test** exercising the reads would convert the
  "cross-compiles, not run" rows into observed behavior as hardware becomes available.
