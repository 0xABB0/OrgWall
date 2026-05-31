# power — todo

Execution checklist and resume point. `spec.md` is authoritative for design.

## Done

- Extracted from the former `sensor` module: power source + low-power mode + caps,
  renamed to the `Mel_Power_*` / `mel_power_*` surface. Per-platform `src/` layout,
  discovered by the build.
- **Augmentation (from `todo.org`): profile + battery.**
  - `Mel_Power_Profile { f32 bias; bool present; }` + `mel_power_profile_current`,
    the OS energy/performance scheme as an ordinal scalar `[-1,+1]` (no enum —
    MEL-CODE-001). Raw OS token via `mel_power_profile_name(buf, cap)` (caller
    buffer; no fixed array, no allocator — MEL-CODE-002/003).
  - Low-power is now **derived** from the profile (`on ⇔ bias < 0`), centralized in
    `src/power.c`; the per-platform low-power primitives were removed.
  - `Mel_Power_Battery { present, charging, level, seconds_to_empty,
    seconds_to_full }` + `mel_power_battery_current`. Overrides the former "charge
    level deliberately absent" stance.
  - `Mel_Power_Caps` gained `battery_present`; `low_power_present` → `profile_present`.
  - Lowerings written for all six platforms (no stubs).
- macOS lowering — built, **run, and observed** (Apple M3 Pro / macOS 26.2):
  source `ac`, profile `bias 0 "Automatic"`, low-power `off` (derived), battery
  `present, level 1.000, not charging`. Self-checking probe `VERDICT PASS`.
- All platforms cross-compile clean (macos, ios, linux, android, win32, wasm).

## Remaining

- **Run-verify on real hosts.** Windows, Linux, iOS, Android are compile/structure-
  verified only. In particular: Linux battery time estimate across `energy_*` vs
  `charge_*` sysfs variants; Windows `BatteryFlag`/`BatteryLifeTime` on a real
  laptop; iOS battery level on device; Android battery intent + `isPowerSaveMode`
  across API levels.
- **Richer profile surfaces (deferred, `spec.md` §6.2).**
  - Windows: `PowerGetEffectiveOverlayScheme` (the Power-mode slider) to map the
    full `[-1,+1]`; needs the `powrprof` link and header availability against the
    cross toolchain.
  - macOS: High Power Mode read (none public today) to raise the ceiling to `+1`.
  - Linux: `power-profiles-daemon` D-Bus (`net.hadess.PowerProfiles`) as primary +
    notification surface, over the `/sys` fallback.
- **Battery time estimates where missing.** Android
  `BatteryManager.computeChargeTimeRemaining()` (API 28+) for `seconds_to_full`.
- **Web.** No synchronous surface; `navigator.getBattery()` is promise-shaped and
  arrives with the deferred event work. Profile has no standardized surface
  (`saveData` is data-saver, not OS profile — must not be conflated).

## Deferred: change notification

Push-style transition callbacks (`spec.md` §5). Not reactor-coupled. The
per-platform notification surfaces are catalogued in `spec.md` §6.

## Refinements

- Battery-absence on Apple desktops (`spec.md` §8): `battery_present` now
  distinguishes a battery-less Mac; confirm no consumer conflates `ac` +
  `!battery_present` with a laptop on AC.
- Android `caps` reports all-present whenever a JNI env exists; tighten by probing
  the methods once where the API level matters.
- The Linux `balanced-performance → 0.5` bias anchor is a judgement call; revisit
  if a consumer needs a differently-weighted axis.

## Known debt carried in from `sensor`

- Public enums (MEL-CODE-001) `Mel_Power_Source` and `Mel_Power_Low_Power_Mode`
  were preserved verbatim by the extraction. The augmentation deliberately did
  **not** add a new enum (profile is a scalar); the two inherited enums remain as
  acknowledged debt pending Gabbo's direction on renaming/reshaping.
