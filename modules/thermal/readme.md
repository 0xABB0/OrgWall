# thermal

OS-level thermal telemetry: a coarse pressure tier and an accurate per-domain
temperature. A standalone top-level module — the platform is a build axis, so the
active OS lowering is chosen by source-directory gating at compile time, not
dispatched through a runtime handle.

Extracted from the former `sensor` module alongside its sibling `power`;
`spec.md` is the design, `todo.md` the execution checklist and resume point.

## Surface

Two pull-only reads and one capability query — global, stateless, no instance,
no reactor:

```c
#include <thermal/thermal.h>

Mel_Thermal_Pressure    mel_thermal_current(void);                       /* unknown|nominal|fair|serious|critical */
Mel_Thermal_Temperature mel_thermal_temperature(Mel_Thermal_Temp_Domain); /* { celsius, fidelity } over primary|cpu|gpu|ambient */
Mel_Thermal_Caps        mel_thermal_caps(void);                          /* { present, temperature-fidelity } */
```

Plus **per-sensor enumeration** (additive; the aggregate above is unchanged) —
one `Mel_Thermal_Sensor` per physical sensor, each pulled on demand, carrying a
`Mel_Degrees` from the `temperature` units module:

```c
const Mel_Alloc* a = mel_alloc_heap();
Mel_Thermal_Sensor_List list = mel_thermal_sensor_enumerate(a);
for (usize i = 0; i < list.count; i++) {
    Mel_Thermal_Reading r = mel_thermal_sensor_read(&list.items[i], NULL);  /* { Mel_Degrees value; fidelity } */
    double c = mel_degrees_to_celsius(r.value);
}
mel_thermal_sensor_list_free(&list, a);
```

See `spec.md` §9. Observed on M3 Pro: ~50 CPU + ~18 GPU `measured` SMC keys plus
ambient. `modules/thermal/example/thermal_sensors.c` is a runnable dump
(`./nob run thermal-sensors`).

The tier's zero value is `unknown` — the honest absence where the OS publishes
nothing. The engine never fabricates `nominal`.

Temperature is the accurate signal where the tier is the portable-coarse one. It
carries a `fidelity` (`measured`/`derived`/`none`): trust `celsius` on `measured`,
treat it as indicative on `derived`, and ignore it on `none` (fall back to the
tier). The engine never fabricates a degree reading — gentle degradation, not a
plausible-looking lie (MEL-ENGINE-VII / VIII).

## Layout

```
modules/thermal/
  spec.md  readme.md  todo.md
  include/
    thermal/thermal.h          tier, temperature, caps
  src/
    apple/  thermal.m caps.c          (macOS + iOS: NSProcessInfo tier; caps)
    macos/  temperature.c             (AppleSMC per-domain temperature)
    ios/    temperature.c             (no SMC in sandbox -> none)
    linux/  thermal.c + ../thermal_sysfs.h  (/sys thermal zones + trip points)
    android/ thermal.c + ../thermal_sysfs.h (JNI PowerManager; /sys temp)
    win32/  thermal.c                 (absent without WMI opt-in)
    web/    thermal.c                 (no synchronous API -> unknown/none)
```

The macOS temperature read needs the IOKit framework and the tier read needs
Foundation; both are linked PUBLIC in `build.c` so dependents inherit them.
Android reaches a `Context` via `ActivityThread.currentApplication()` and uses the
JNI bootstrap in the `platform` module (`mel_platform_android_env`).

## Status

Every platform has a real lowering — no stubs. Verification by what this host
could exercise:

- **macOS** — built, run, observed (M3 Pro / macOS 26.2): tier `nominal`, CPU ≈
  50 °C / GPU ≈ 42 °C `measured`, ambient ≈ 33 °C `derived`.
- **Linux** — `/sys` thermal zones + trip points; cross-compiles clean, not run.
- **iOS** — tier via `NSProcessInfo`; temperature `none` (no SMC). Cross-compiles
  against the iphoneos SDK, not run.
- **Android** — JNI `PowerManager.getCurrentThermalStatus`; `/sys` temperature.
  TU checked against `jni.h`, not run on a device.
- **Windows** — absent by default (WMI opt-in, see `spec.md`); cross-compiles.
- **Web** — no synchronous OS surface; honest `unknown`/`none` until the deferred
  event work (see `spec.md`).

Change notification (push) is deferred and, when it lands, will not be
reactor-coupled. `todo.md` carries the augmentation direction (a
`Mel_Thermal_Sensor` enumeration surface, a units module).
