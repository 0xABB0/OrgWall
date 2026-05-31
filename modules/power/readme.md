# power

OS-level power telemetry: power source (AC vs battery) and low-power / battery-
saver mode. A standalone top-level module — the platform is a build axis, so the
active OS lowering is chosen by source-directory gating at compile time, not
dispatched through a runtime handle.

Extracted from the former `sensor` module alongside its sibling `thermal`;
`spec.md` is the design, `todo.md` the execution checklist and resume point.

## Surface

Two pull-only reads and one capability query — global, stateless, no instance,
no reactor:

```c
#include <power/power.h>

Mel_Power_Source         mel_power_source_current(void);    /* unknown|ac|battery */
Mel_Power_Low_Power_Mode mel_power_low_power_current(void); /* unknown|off|on */
Mel_Power_Caps           mel_power_caps(void);              /* { power_source_present, low_power_present } */
```

Each read's zero value is `unknown` — the honest absence where the OS publishes
nothing. The engine never fabricates `ac` / `off`.

Power source and low-power mode are independent: a laptop on AC may sit in
user-initiated Low Power Mode; a phone at 90 % may sit in Low Power Mode the user
toggled to conserve for later. The two reads are independent and the consumer
joins them as it wishes (MEL-ENGINE-IX).

## Layout

```
modules/power/
  spec.md  readme.md  todo.md
  include/
    power/power.h              source, low-power, caps
  src/
    apple/  lowpower.m caps.m          (macOS + iOS: NSProcessInfo)
    macos/  power.m                    (IOKit IOPS power source)
    ios/    power.m                    (UIDevice battery state)
    linux/  power.c + ../power_sysfs.h (/sys power_supply, platform_profile)
    android/ power.c                   (JNI BatteryManager/PowerManager)
    win32/  power.c                    (GetSystemPowerStatus)
    web/    power.c                    (no synchronous API -> unknown)
```

The macOS power-source read needs IOKit + Foundation, iOS needs UIKit +
Foundation; all are linked PUBLIC in `build.c` so dependents inherit them. Android
reaches a `Context` via `ActivityThread.currentApplication()` and uses the JNI
bootstrap in the `platform` module (`mel_platform_android_env`).

## Status

Every platform has a real lowering — no stubs. Verification by what this host
could exercise:

- **macOS** — built, run, observed (M3 Pro / macOS 26.2): source `ac` (plugged) /
  `battery` (unplugged), low-power `off`.
- **Windows** — `GetSystemPowerStatus`; cross-compiles against the Windows headers,
  not run.
- **Linux** — `/sys` power-supply + `platform_profile`; cross-compiles clean, not run.
- **iOS** — `UIDevice.batteryState`; cross-compiles against the iphoneos SDK, not run.
- **Android** — JNI `BatteryManager` / `PowerManager`; TU checked against `jni.h`,
  not run on a device.
- **Web** — no synchronous OS surface; honest `unknown` until the deferred event
  work (see `spec.md`).

Change notification (push) is deferred and, when it lands, will not be
reactor-coupled. Charge level is deliberately absent (see `spec.md` §4.3).
