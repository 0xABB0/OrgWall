# Sensor module extraction → `thermal` + `power`

`todo.org` line 10: *"Extract thermal and power from sensor module into a power
module and thermal module."* Scoped by Gabbo to **extraction only** — the two
augmentation items (lines 11–12: `Mel_Thermal_Sensor` enumeration + units module;
power profile + battery level) were explicitly deferred.

## Work done

- **Dissolved `modules/sensor/` into two standalone top-level modules**
  `modules/thermal/` and `modules/power/`. Decisions confirmed with Gabbo before
  the move: (a) dissolve `sensor` entirely rather than keep an umbrella; (b) rename
  symbols to the module prefix.
- **Renamed the public surface** mechanically:
  - `Mel_Sensor_Thermal_Pressure` → `Mel_Thermal_Pressure`;
    `mel_sensor_thermal_current` → `mel_thermal_current`;
    `Mel_Sensor_Temperature` / `_Temp_Domain` / `_Temp_Fidelity` →
    `Mel_Thermal_Temperature` / `Mel_Thermal_Temp_Domain` / `Mel_Thermal_Temp_Fidelity`;
    `mel_sensor_thermal_temperature` → `mel_thermal_temperature`.
  - `Mel_Sensor_Power_Source` → `Mel_Power_Source`;
    `Mel_Sensor_Low_Power_Mode` → `Mel_Power_Low_Power_Mode`;
    `mel_sensor_power_source_current` → `mel_power_source_current`;
    `mel_sensor_low_power_current` → `mel_power_low_power_current`.
- **Split the cross-cutting `Mel_Sensor_Caps`** into two per-module structs:
  `Mel_Thermal_Caps { present, temperature }` via `mel_thermal_caps()`, and
  `Mel_Power_Caps { power_source_present, low_power_present }` via `mel_power_caps()`.
  Each platform's single old `mel_sensor_caps` became two functions.
- **Each module is self-contained.** Headers fold caps into the one public header
  (`<thermal/thermal.h>`, `<power/power.h>`) — the umbrella `<sensor/sensor.h>` and
  the separate `caps.h` are gone. `build.c` per module declares its own framework
  links **PUBLIC** so dependents inherit them (see correctness fix below).
- **Shared sysfs helpers duplicated, not cross-depended.** The old
  `src/sensor_sysfs.h` split into `thermal/src/thermal_sysfs.h` (tier, temperature,
  presence) and `power/src/power_sysfs.h` (source, presence). The generic
  `mel_sysfs_read_str` is `static inline` in both — internal linkage, separate TUs,
  no ODR clash — so the two modules stay orthogonal (no thermal↔power edge). Same
  for the Android JNI helpers (`mel_android_app_context` / `_system_service`),
  duplicated into each module's `android/` source.
- **Docs migrated, not discarded.** The sensor `spec.md`/`readme.md`/`todo.md`
  design rationale was split along the thermal/power seam into each new module
  (`spec.md` §-by-§, plus `readme.md` and `todo.md`). The augmentation directions
  (lines 11–12) are recorded in each module's `todo.md` as the resume point.
- **Verification.** Both modules cross-compile clean on all six platforms (macos,
  ios, linux, android, win32, wasm). On macOS I linked a throwaway driver against
  the built static libs and **ran it**: tier `nominal`, CPU ≈ 49 °C / GPU ≈ 42 °C
  `measured`, ambient `derived`, power source `ac` (plugged), low-power `off`, caps
  all sensible — identical semantics to the pre-extraction `sensor` reads. Sources
  formatted with the repo `.clang-format` (MEL-CODE-004); rebuilt and re-ran after
  formatting to confirm.
- Marked `todo.org` line 10 DONE; left lines 11–12 (augmentation) untouched.

## Two latent bugs in the old `sensor/build.c`, fixed in passing

1. **iOS dropped a translation unit.** The iOS gate globbed only `src/ios/*.m`, but
   `ios/temperature.c` is a `.c` file — so `mel_sensor_thermal_temperature` would
   have been **undefined at iOS link**. Never caught because iOS was only ever
   TU-compiled, never fully linked. The `thermal` build now globs `src/ios/*.c`
   (+ `src/apple/*.c`/`.m`); `ios/temperature.o` now compiles.
2. **`web/` sources were never gated.** `sensor/build.c` had no `WASM` line at all,
   so `src/web/sensor.c` was dead. Both new modules gate `WHEN(MEL_ON(WASM))` and
   wasm now builds the web lowering.

## Kludges (MEL-ENGINE-VIII — confess all)

- **Pre-existing debt carried verbatim, not fixed.** The extracted code keeps
  public **enums** (MEL-CODE-001) and the fixed `MEL_SMC_MAX_SENSORS[96]` array
  (MEL-CODE-002). Both violate the coding guidelines, but rewriting them is redesign,
  not extraction — explicitly out of the scoped task. Flagged in each module's
  `todo.md` under "Known debt carried in from `sensor`." No new enum or fixed array
  was introduced.
- **Framework links: a behavioral change, not a pure move.** The old
  `sensor/build.c` linked **no** frameworks, and its `readme.md` claimed they lived
  in a `modules/build.c` that **does not exist** (the real IOKit link is in
  `modules/display/build.c`). So a hypothetical app depending on `sensor` would have
  failed to final-link on Apple. I gave each new module its own correct PUBLIC links
  (thermal: Foundation+IOKit on macOS, Foundation on iOS; power: Foundation+IOKit on
  macOS, Foundation+UIKit on iOS). This is strictly more correct but **is** a
  deviation from "move only" — calling it out as such. No app exercises the final
  link yet, so it is verified only by my standalone driver (macOS).
- **Helper duplication.** `mel_sysfs_read_str` and the two Android JNI helpers now
  exist in both modules. Chosen over a shared module to keep thermal and power
  orthogonal (MEL-ENGINE-IX). It is genuine duplication (~10–40 lines each); the
  alternative (a tiny shared `sysfs`/`android-jni` module) is the escape hatch if it
  grows.
- **`Mel_Power_Low_Power_Mode` reads awkwardly** (`power_low_power`). It is the
  mechanical `Sensor`→`Power` rename of `Mel_Sensor_Low_Power_Mode`. Left as-is to
  keep the rename faithful; noted in `power/todo.md` for Gabbo to rename during
  augmentation.

## Not done (out of scope / flagged for follow-up)

- **`design/*.md` still say `sensor`.** `gpu-rhi.md`, `frame-pacing.md`, `xr.md`,
  `media-video.md`, `render-graph.md`, etc. reference the `sensor` module and
  `caps.power.thermal_pressure`-style re-exports. These are design specs, not code;
  no code includes them. I did **not** rewrite them — a sweeping doc edit risks
  diverging from your intent. Recommend a follow-up doc pass to retarget them at
  `thermal`/`power` if/when those consumers land.
- The augmentation items (lines 11–12) — `Mel_Thermal_Sensor` +
  `mel_thermal_sensor_enumerate` + a temperature **units** module mirroring
  `time.frequency`; power profile + battery level — are recorded as resume points,
  not implemented.

## CLAUDE.md suggestions (recommendations only — not applied)

- The module-folder convention in `melody/CLAUDE.md` lists `public/ private/ src/
  meta/`, but live modules (time, display, **and** the old sensor) use
  `include/<subtree>/` rather than `public/`. Worth reconciling the doc with the
  actual idiom so new modules don't guess.

## Suggestions

- **Stale `readme.md` claim is a smell worth a lint.** The sensor readme asserted a
  framework link in a nonexistent `modules/build.c`. A cheap check — "every
  `-framework`/OS-API a module's sources reference is declared in its own
  `build.c`" — would have caught both the missing IOKit link and the iOS dropped-TU
  bug at discovery time.
- **A `module-skeleton` generator** (build.c + include/<m>/<m>.h + src axis dirs +
  readme/spec/todo stubs) would make "extract into a new module" a one-shot, and
  would bake in the `WASM` gate that the sensor module forgot.
- If thermal/power augmentation proceeds, the **units module** (Celsius/Fahrenheit/
  Kelvin conversions) mirroring `time.frequency` is the no-prerequisite piece to
  build first, per the repo's new-feature workflow.
