# power — todo

Execution checklist and resume point. `spec.md` is authoritative for design.

## Done

- Extracted from the former `sensor` module: power source + low-power mode + caps,
  renamed to the `Mel_Power_*` / `mel_power_*` surface. Per-platform `src/` layout,
  discovered by the build.
- Read API: `mel_power_source_current`, `mel_power_low_power_current`,
  `mel_power_caps`.
- macOS lowering — built, **run, and observed** (Apple M3 Pro / macOS 26.2):
  power source IOKit `IOPSCopyPowerSourcesInfo` + `IOPSGetProvidingPowerSourceType`
  (`ac` when plugged), low-power `NSProcessInfo.isLowPowerModeEnabled` (guarded by
  `respondsToSelector:`, `off`).
- All platforms cross-compile clean (macos, ios, linux, android, win32, wasm).

## Remaining

- **Run-verify on real hosts.** Windows, Linux, iOS, Android are compile/structure-
  verified only. Windows `SystemStatusFlag` on pre-/post-1709; Linux power-supply
  typing + `platform_profile`; iOS battery state on device; Android battery intent
  + `isPowerSaveMode` across API levels.
- **Linux UPower path.** The `/sys` reads are the fallback; the richer
  `org.freedesktop.UPower` D-Bus interface (`OnBattery`, `PowerProfiles`) is the
  intended primary + notification surface. Wire it where `upowerd` is present.
- **Web.** No synchronous surface exists; `navigator.getBattery()` is promise-shaped
  and arrives with the deferred event work. Low-power has no standardized surface
  (`saveData` is data-saver, not OS low-power — must not be conflated).

## Deferred: change notification

Push-style transition callbacks (`spec.md` §5). Not reactor-coupled. The
per-platform notification surfaces are catalogued in `spec.md` §6.

## Refinements

- Battery-absence reporting (`spec.md` §8): decide whether `caps` should reflect
  battery hardware presence on a battery-less desktop, distinct from `unknown`.
- Android `caps` reports both present whenever a JNI env exists; tighten by probing
  the methods once where the API level matters.

## Augmentation (from `todo.org`)

- Where possible, give information on the power profile and the battery level. This
  is augmentation, not part of the extraction that created this module; charge
  level is deliberately absent today (`spec.md` §4.3) and would land with explicit
  consent / permission semantics.

## Known debt carried in from `sensor`

- Public enums (MEL-CODE-001) were preserved verbatim by the extraction. The
  low-power type reads `Mel_Power_Low_Power_Mode` (mechanical `Sensor`→`Power`
  rename); revisit the naming under Gabbo's direction when augmenting.
