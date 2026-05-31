# power

OS-level power telemetry: power source (AC vs battery), the energy/performance
profile, and the battery's state of charge. A standalone top-level module — the
platform is a build axis, so the active OS lowering is chosen by source-directory
gating at compile time, not dispatched through a runtime handle.

Extracted from the former `sensor` module alongside its sibling `thermal`;
`spec.md` is the design, `todo.md` the execution checklist and resume point.

## Surface

Pull-only reads and one capability query — global, stateless, no instance,
no reactor:

```c
#include <power/power.h>

Mel_Power_Source         mel_power_source_current(void);       /* unknown|ac|battery */
Mel_Power_Profile        mel_power_profile_current(void);      /* { bias, present } */
bool                     mel_power_profile_name(char*, usize); /* raw OS token -> caller buffer */
Mel_Power_Low_Power_Mode mel_power_low_power_current(void);    /* derived: on <=> bias < 0 */
Mel_Power_Battery        mel_power_battery_current(void);      /* { present, charging, level, to_empty, to_full } */
Mel_Power_Caps           mel_power_caps(void);                 /* { source, profile, battery } presence */
```

Each read's zero value is the honest absence — `unknown` for source,
`present = false` for profile and battery. The engine never fabricates `ac`, a
profile bias, or a charge level.

## Profile

`Mel_Power_Profile` is the OS energy/performance scheme expressed as an **ordinal
scalar**, not a closed enum (MEL-CODE-001): `bias ∈ [-1, +1]`, where `-1` is
maximum power-saving, `0` balanced, `+1` maximum performance. `present = false`
where the build's platform publishes no profile surface; ignore `bias` then.

The scalar is the portable signal; `mel_power_profile_name` fills the caller's
buffer with the verbatim OS token (`"balanced-performance"`, `"Low Power"`, …) —
the faithful escape beneath the portable axis (MEL-ENGINE-II). It returns `false`
where no profile exists or the buffer is too small (no silent truncation,
MEL-ENGINE-VIII).

Only Linux exposes a genuinely graded profile (`platform_profile`); macOS, iOS,
Windows and Android publish only the binary battery-saver bit, so their `bias`
ranges over `{-1, 0}`. The richer per-OS surfaces (Windows overlay scheme, macOS
High Power Mode, `power-profiles-daemon`) are catalogued as deferred in `spec.md`.

`mel_power_low_power_current` is retained as a **derived** view over the profile
(`on ⇔ bias < 0`); it is no longer a separate primitive (MEL-ENGINE-IX).

## Battery

`Mel_Power_Battery` carries `present`, `charging`, `level ∈ [0,1]`, and two time
estimates in seconds: `seconds_to_empty` and `seconds_to_full`. A negative time
means the OS does not publish that estimate; `present = false` means no battery or
no surface — ignore the remaining fields. This overrides the former stance
(charge level deliberately absent); it is exposed at Gabbo's direction.

## Layout

```
modules/power/
  spec.md  readme.md  todo.md
  include/
    power/power.h              source, profile, low-power, battery, caps
  src/
    power.c                    low-power derived from the profile (all platforms)
    power_str.h                name-copy helper
    apple/  profile.m          (macOS + iOS: NSProcessInfo low-power -> bias)
    macos/  power.m            (IOKit IOPS source + battery + caps)
    ios/    power.m            (UIDevice battery state/level + caps)
    linux/  power.c + ../power_sysfs.h (/sys power_supply, platform_profile)
    android/ power.c           (JNI BatteryManager intent / PowerManager)
    win32/  power.c            (GetSystemPowerStatus)
    web/    power.c            (no synchronous API -> absent)
```

The macOS reads need IOKit + Foundation, iOS needs UIKit + Foundation; all are
linked PUBLIC in `build.c` so dependents inherit them. Android reaches a `Context`
via `ActivityThread.currentApplication()` and uses the JNI bootstrap in the
`platform` module (`mel_platform_android_env`).

## Status

Every platform has a real lowering — no stubs. Verification by what this host
could exercise:

- **macOS** — built, run, observed (M3 Pro / macOS 26.2): source `ac` (plugged),
  profile `present, bias 0, "Automatic"` (saver off), low-power `off` (derived),
  battery `present, level 1.000, not charging, times -1` (full on AC); the
  derived-low-power and caps-mirror invariants hold (`VERDICT PASS`).
- **Windows** — `GetSystemPowerStatus` source/saver/battery; cross-compiles, not run.
- **Linux** — `/sys` power-supply + `platform_profile` + battery `capacity`/`status`/
  energy-or-charge time estimate; cross-compiles clean, not run.
- **iOS** — `UIDevice` battery state/level; profile via `NSProcessInfo`. Cross-compiles
  against the iphoneos SDK, not run.
- **Android** — JNI `BatteryManager` intent + `PowerManager.isPowerSaveMode`; TU
  checked against `jni.h`, not run on a device.
- **Web** — no synchronous OS surface; honest absence until the deferred event work.

Change notification (push) is deferred and, when it lands, will not be
reactor-coupled.
