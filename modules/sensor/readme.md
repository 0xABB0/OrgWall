# sensor

OS-level environmental telemetry: thermal pressure, power source, and low-power
mode. A standalone top-level module — not a child of `platform`, and with no
runtime platform object. The platform is a build axis, so the active OS lowering
is chosen by source-directory gating at compile time.

`spec.md` is the design; `todo.md` is the execution checklist and resume point.

## Surface

Three pull-only reads and one capability query — global, stateless, no instance,
no reactor:

```c
#include <sensor/sensor.h>   /* umbrella: thermal + power + caps */

Mel_Sensor_Thermal_Pressure mel_sensor_thermal_current(void);      /* unknown|nominal|fair|serious|critical */
Mel_Sensor_Temperature      mel_sensor_thermal_temperature(Mel_Sensor_Temp_Domain); /* { celsius, fidelity } over primary|cpu|gpu|ambient */
Mel_Sensor_Power_Source     mel_sensor_power_source_current(void); /* unknown|ac|battery */
Mel_Sensor_Low_Power_Mode   mel_sensor_low_power_current(void);    /* unknown|off|on */
Mel_Sensor_Caps             mel_sensor_caps(void);                 /* presence bits + temperature fidelity */
```

Each tier's zero value is `unknown` — the honest absence where the OS publishes
nothing. The engine never fabricates `nominal`/`ac`/`off`.

Temperature is the accurate signal where the tier is the portable-coarse one. It
carries a `fidelity` (`measured`/`derived`/`none`): trust `celsius` on `measured`,
treat it as indicative on `derived`, and ignore it on `none` (fall back to the
tier). The engine never fabricates a degree reading — gentle degradation, not a
plausible-looking lie (MEL-ENGINE-VII / VIII).

Headers: `<sensor.thermal/thermal.h>`, `<sensor.power/power.h>`, `<sensor/caps.h>`,
and the umbrella `<sensor/sensor.h>`.

## Layout

```
modules/sensor/
  spec.md  readme.md  todo.md
  include/
    sensor.thermal/thermal.h   Mel_Sensor_Thermal_Pressure + thermal read
    sensor.power/power.h       Mel_Sensor_Power_Source, Mel_Sensor_Low_Power_Mode + reads
    sensor/caps.h              Mel_Sensor_Caps + mel_sensor_caps
    sensor/sensor.h            umbrella
  src/
    apple/  thermal.m lowpower.m caps.m   (macOS + iOS: NSProcessInfo)
    macos/  power.m temperature.c          (IOKit IOPS; SMC temperature)
    ios/    power.m temperature.c          (UIDevice battery; no SMC -> none)
    linux/  sensor.c + ../sensor_sysfs.h    (/sys thermal/power/platform_profile)
    android/ sensor.c                       (JNI PowerManager/BatteryManager; /sys temp)
    win32/  sensor.c                        (GetSystemPowerStatus)
    web/    sensor.c                        (no synchronous API -> unknown/none)
```

Sources are auto-discovered (`mel_build_add_modules`). The macOS power and
temperature reads need the IOKit framework, linked in `modules/build.c`. Android
reaches a `Context` via `ActivityThread.currentApplication()` and uses the JNI
bootstrap in the `platform` module (`mel_platform_android_env`).

## Status

Every platform has a real lowering — no stubs. Verification by what this host
could exercise:

- **macOS** — built, run, observed (M3 Pro / macOS 26.2): tier `nominal`, power
  `battery`, CPU ≈ 45 °C / GPU ≈ 37 °C `measured`, ambient `derived`.
- **Windows** — `GetSystemPowerStatus`; compiles against Windows headers (mingw),
  not run.
- **Linux** — `/sys` thermal zones + trip points, power-supply, platform-profile;
  clean under `-Wall -Wextra`, not run.
- **iOS** — `UIDevice.batteryState`; compiles against the iphoneos SDK, not run.
- **Android** — JNI `PowerManager`/`BatteryManager`; TU checked against `jni.h`,
  not run on a device.
- **Web** — no synchronous OS surface; honest `unknown`/`none` until the deferred
  event work (see `spec.md` §6).

Change notification (push) is deferred and, when it lands, will not be
reactor-coupled.
