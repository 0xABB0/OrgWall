# Thermal sensor augmentation + `temperature` units module

## Work done

Implemented the `todo.org` thermal augmentation: a `temperature` units module and
per-sensor thermal enumeration. Design captured in
`design/thermal-sensor-augmentation.md`; four forks resolved with Gabbo up front
(`Mel_Real` backing, standalone top-level module, reuse existing enums, one
physical sensor each + conveniences).

**New `temperature` module** (`modules/temperature/`, standalone top-level, peer of
`math`). Header-only (`core`/`easing` precedent): `temperature.h` + `.inl`, no
`.c`. `Mel_Degrees { Mel_Real v }`, kelvin-canonical, mirroring `time.frequency`'s
shape. Exact Celsius/Fahrenheit/Kelvin conversions via `mel_real` rational adds and
`mul_ui`/`div_ui` — no lossy `double` scale factor. General-purpose arithmetic /
compare subset; the music-only ops of `time.frequency` deliberately omitted.

**Thermal enumeration** (`modules/thermal/`). Added to `thermal.h`:
`Mel_Thermal_Reading { Mel_Degrees value; Mel_Thermal_Temp_Fidelity fidelity }`,
`Mel_Thermal_Sensor { name, domain, get, handle }` with a
`get(self, user) → reading` callback, `Mel_Thermal_Sensor_List`,
`mel_thermal_sensor_enumerate(alloc)`, `mel_thermal_sensor_list_free`, and the
`mel_thermal_sensor_read` convenience. Additive — the aggregate
`mel_thermal_temperature(domain)` is untouched. The list is one contiguous
allocation (items + name bytes) so free is a single `mel_dealloc`
(`src/sensor.c`, `ALWAYS`). Per-platform lowerings: macOS enumerates the memoized
SMC die keys (one sensor per classified `flt ` key); Linux/Android enumerate
`/sys/class/thermal/thermal_zone*` (shared `mel_sysfs_sensor_enumerate` in
`thermal_sysfs.h`); iOS/Windows/Web return an empty list. A `none` reading carries
`0 K` — the absolute-zero sentinel, never a fabricated number.

**Verification.** `temperature-example` and `thermal-sensors` example executables
(own `main` + `assert`). macОС M3 Pro: enumerated ~50 CPU + ~18 GPU `measured` SMC
keys plus ambient (`derived`/`none`), values in range, sentinel honoured.
Conversions exact (0 °C=32 °F=273.15 K, 100 °C=212 °F, −40 °C=−40 °F). **All six
first-class targets build clean** (`./nob build thermal <plat>` → exit 0);
`temperature-example.wasm` and `thermal-sensors.wasm` run under node.

**Framework fix (required for wasm).** The `Mel_Real` backing pulls `mpfr`/`gmp`
into `thermal`. The wasm build initially failed: gmp's autotools install ran the
**host** `ranlib` on emscripten objects → `LLVM ERROR: malformed uleb128`. Fixed
in `modules/build/thirdparty.c` — the autotools cross-configure now passes
`AR=emar RANLIB=emranlib` for the wasm platform (general; applies to any autotools
third-party). `nob` self-rebuilds via its watch list.

## Kludges (full confession — the bar is zero)

- **Enums reused (MEL-CODE-001).** `Mel_Thermal_Reading` carries the
  `Mel_Thermal_Temp_Fidelity` enum; sensors carry `Mel_Thermal_Temp_Domain`.
  Sanctioned — Gabbo chose "reuse existing enums as-is." De-enuming the module
  stays a separate pass.
- **`Mel_Real` backing weight.** `Mel_Degrees` wraps 256-bit MPFR for what is
  affine f32-origin sensor data; this drags `mpfr`/`gmp` into a platform-telemetry
  module (against the module's own "keep lean" note). Sanctioned — Gabbo chose the
  faithful mirror over the f32/f64 options I presented. It is exactness no
  temperature use-case needs.
- **`handle` is a pointer through an integer.** On macOS
  `Mel_Thermal_Sensor.handle` (`u64`) holds `(uintptr_t)&static_smc_entry`. Valid
  only because the SMC groups are process-lifetime static. A backend needing a
  non-pointer or >64-bit datum would not fit.
- **`MEL_SMC_MAX_SENSORS` fixed array (MEL-CODE-002).** Carried verbatim from
  `sensor`; not touched. Enumeration reads from it but did not fix it.
- **Small fixed local arrays.** macOS enumerate uses `groups[3]`/`domains[3]` to
  iterate the three SMC groups, and the `/sys` code uses `char[64]` path/type
  scratch (matching existing code). Not capacity arrays, but technically fixed —
  flagged.
- **`/sys` enumerate is two-pass and races on hotplug.** Pass 1 counts zones +
  name bytes, pass 2 fills; a zone appearing/disappearing between passes is
  tolerated (`n < count` guard) but enumeration is best-effort, not atomic.
- **MEL_TEST harness not used.** `mel_add_test` only sets `is_test`; it does not
  auto-link the `tools/test/src/runner.c` runner (no injection in the build), and
  the documented `./nob test` path is the half-wired harness. I verified with
  `mel_add_executable` examples (own `main`) instead — a workaround, not the
  intended test path.
- **Framework touched from a feature task.** `modules/build/thirdparty.c` was
  edited to make wasm build. Necessary (wasm is first-class and must compile), but
  it is a build-system change folded into a module feature; called out so it is
  not silent.

## CLAUDE.md suggestions (recommendations only — not applied)

- Document that `mel_add_test` does not wire the `tools/test` runner; either wire
  it (inject the runner + `test` include for `is_test` targets) or note that
  test targets must supply their own `main`. Right now `<test/test.h>` + the
  `MEL_TEST` macros have no runnable path through `./nob test`.
- Note the wasm autotools requirement (`AR=emar RANLIB=emranlib`) in
  `modules/build/platforms.md` alongside the existing `emcc`/`emar` line.

## Suggestions

- **De-enum pass for `thermal`** (pressure/domain/fidelity) per MEL-CODE-001, and
  retire `MEL_SMC_MAX_SENSORS` for a dynamic array (MEL-CODE-002) — both carried
  debt, now exercised by the new surface.
- **Generalize the toolchain.** Add `ranlib` to `Mel_Toolchain` and pass
  `AR`/`RANLIB` for every cross target from the struct, instead of the wasm
  special-case in `thirdparty.c`.
- **`temperature` exact-out accessors.** It currently exposes `view` (kelvin) +
  three `to_<unit>` doubles; an `mpfr_ptr`-out variant per unit would let a
  consumer keep full precision through a conversion, matching the exactness the
  `Mel_Real` backing already pays for.
- **Wire the spec §8 consumers.** Nothing yet depends on `thermal`; `gpu` /
  `frame.pacing` re-exporting the tier would validate the dependency direction
  (and confirm the mpfr propagation is acceptable downstream).
