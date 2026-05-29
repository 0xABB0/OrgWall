# sensor — todo

Execution checklist and resume point. `spec.md` is authoritative for design.

## Done

- Module scaffold: headers (`sensor.thermal`, `sensor.power`, `sensor/caps`,
  umbrella), per-platform `src/` layout, auto-discovered by the build.
- Read API: `mel_sensor_thermal_current`, `mel_sensor_thermal_temperature`,
  `mel_sensor_power_source_current`, `mel_sensor_low_power_current`,
  `mel_sensor_caps`.
- macOS lowering — built, **run, and observed** on Apple M3 Pro / macOS 26.2
  (not merely linked): thermal tier `NSProcessInfo.thermalState`, low-power
  `NSProcessInfo.isLowPowerModeEnabled` (guarded by `respondsToSelector:`), power
  source IOKit `IOPSCopyPowerSourcesInfo` + `IOPSGetProvidingPowerSourceType`,
  and SMC per-domain temperature (`AppleSMC`, no entitlement) — CPU/GPU
  `measured`, ambient `derived`, key set memoized after a one-time enumeration.
- Temperature API with `measured`/`derived`/`none` fidelity for gentle
  degradation; `caps.temperature` reports the achievable primary-domain fidelity.
- All platforms implemented (no stubs). Verification by what this host could run:
  - **Windows** — `GetSystemPowerStatus` power source + low-power. Compiles
    against the Windows headers (mingw on this host); not run.
  - **Linux** — thermal tier (zone trip points), temperature (`/sys/class/thermal`),
    power source (`/sys/class/power_supply`), low-power
    (`/sys/firmware/acpi/platform_profile`); shared `src/sensor_sysfs.h`. Clean
    under `-Wall -Wextra`; not run.
  - **iOS** — `UIDevice.batteryState` power source; thermal/low-power shared from
    `apple/`. Compiles against the iphoneos SDK; not run.
  - **Android** — JNI `PowerManager.getCurrentThermalStatus` / `isPowerSaveMode`,
    `BATTERY_CHANGED` sticky-intent `plugged`, via `ActivityThread.currentApplication()`;
    temperature via `/sys` (`derived`). TU checked against `jni.h`; not run.
- IOKit framework link added in `modules/build.c` (macOS).

## Remaining

- **Run-verify on real hosts.** Windows, Linux, iOS, Android are compile/structure-
  verified only. Run each and confirm values: Linux zone typing + trip-point tier
  mapping; Windows `SystemStatusFlag` on pre-/post-1709; iOS battery state on
  device; Android thermal-status across API levels (the call needs API 29+; below
  that it throws and we report `unknown`).
- **macOS temperature accuracy.** Validate family means against `powermetrics`
  across load and on non-M3 chips (M1/M2/M4, Intel `sp78`). Ambient (`TA*`/`Ta*`)
  is best-effort `derived`; confirm or drop.
- **Android privileged temperature.** `HardwarePropertiesManager.getDeviceTemperatures`
  is `measured` but device/profile-owner only; offer it where the app qualifies,
  falling back to the `/sys` `derived` path.
- **Windows thermal.** Tier + temperature need the WMI `MSAcpi_ThermalZoneTemperature`
  / `IOCTL_THERMAL_QUERY_INFO` opt-in (nontrivial poll cost); absent by default.
- **Web.** No synchronous surface exists; Compute Pressure (`PressureObserver`) and
  `navigator.getBattery()` are async and arrive with the deferred event work.
  Low-power has no standardized surface at all (`saveData` is data-saver, not OS
  low-power — must not be conflated).

## Deferred: change notification

Push-style transition callbacks (`spec.md` §6). Not reactor-coupled. Design the
plain callback contract (threading, coalescing) when this lands. The per-platform
notification surfaces are catalogued in `spec.md` §7. Several platforms (Web,
parts of Linux) are *only* observable through async/event surfaces, so their full
fidelity arrives with this work.

## Refinements

- Android `caps` reports `thermal_present`/`low_power_present` = true whenever a
  JNI env exists, but `getCurrentThermalStatus` needs API 29+; on older levels the
  read returns `unknown` while caps says present. Tighten by probing the method
  once.
- Battery-absence reporting: `spec.md` §5.1 wants a battery-less desktop's
  absence surfaced through `caps`, distinct from `unknown`. macOS currently
  reports `power_source_present = true` and the providing-source read returns
  `ac`. Decide whether caps should reflect battery hardware presence.
- SMC read cost: a `cpu` temperature read does one `READ_BYTES` syscall per
  classified sensor (~60 on M3 Pro). Fine at telemetry cadence, not per-frame.
  If a consumer samples hot, add a min-interval cache (last value + timestamp);
  needs a clock, which would introduce a `time`-module dependency — weigh
  against keeping `sensor` core-only.
- The SMC connection (`io_connect_t`) is opened lazily on first temperature read
  and held for process lifetime. No `mel_sensor_shutdown` exists; add one if a
  consumer needs deterministic teardown.

## Testing

No automated test yet. Host verification is building on macOS and observing the
reads. A portable test harness needs a way to spoof the OS surface per platform —
gate on the same mechanism the deferred event work introduces.
