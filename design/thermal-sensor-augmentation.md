# Thermal sensor augmentation + `temperature` units module

Freeform design for the `todo.org` line:

> Augment the thermal module: we should have a `Mel_Thermal_Sensor` struct,
> available through a `mel_thermal_sensor_enumerate` function. each thermal sensor
> has a callback called `get(Mel_Thermal_Sensor* self, void* user) ->
> Mel_Thermal_Reading {Mel_Degrees, Mel_Fidelity}` (new temperature module,
> mirroring `time.frequency`, exposing function to handle temperature stuff,
> converting between the three main units of measurements).

Decisions taken with Gabbo (the four forks of the spec):

1. **`Mel_Degrees` backing** — `Mel_Real` (256-bit MPFR), the faithful mirror of
   `Mel_Hz`. Consequence accepted: `thermal` now transitively pulls `temperature
   → math → mpfr → gmp`. See §5 for the cross-compile fallout this introduces.
2. **Placement** — `temperature` is a **standalone top-level module**
   (`modules/temperature/`), a peer of `math`/`color`, not a sub-namespace of
   `thermal`. `thermal` depends on it.
3. **Enums** — reuse the existing `Mel_Thermal_Temp_Fidelity` /
   `Mel_Thermal_Temp_Domain` verbatim in the new surface. De-enuming the module
   (MEL-CODE-001) stays a separate, later pass.
4. **Sensor granularity** — one `Mel_Thermal_Sensor` per *physical* sensor (each
   `/sys` thermal zone, each classified SMC die key), **additive**: the existing
   `mel_thermal_temperature(domain)` aggregate accessor is untouched. Plus the
   read convenience of §3.

---

## 1. `temperature` module — units, mirroring `time.frequency`

`time.frequency` is `Mel_Hz { Mel_Real v }` plus constructors / a `to_double` /
arithmetic / comparisons, mostly `static inline` over `math/real`. `temperature`
mirrors that *shape*; it drops the music-only operations (`transpose_*`,
`octave_*`, `harmonic`, `mod`, `floordiv`, `beat`) because they carry no
temperature meaning (MEL-ENGINE-IX: compose what is meaningful, do not bolt on
nonsense).

```c
typedef struct { Mel_Real v; } Mel_Degrees;   /* canonical: kelvin */
```

**Canonical unit is kelvin** because: it is the SI base; absolute zero `= 0`
gives an unambiguous sentinel (`mel_degrees_is_absolute_zero`) distinct from a
real 0 °C reading (`= 273.15 K`); and multiplicative/ratio operations are
physically meaningful only on an absolute scale. Celsius⇄Kelvin is then a pure
exact add.

**The three units.** Constructors and accessors for Celsius, Fahrenheit, Kelvin.
Every conversion is **exact** — expressed entirely through `mel_real`'s
exact-rational adds, `mul_ui`/`div_ui`, never a lossy `double` scale factor:

- `°C → K`:  `K = C + 27315/100`
- `°F → K`:  `K = (F + 45967/100) · 5 / 9`
- `K → °C`:  `C = K − 27315/100`
- `K → °F`:  `F = K · 9 / 5 − 45967/100`

Exactness is the whole reason `Mel_Real` earns its keep here (§4 of the module
spec). A header-only module (`core`/`easing` precedent) — no `.c`, all `inline`,
`mel_depends("math")`.

## 2. `Mel_Thermal_Reading`

```c
typedef struct {
    Mel_Degrees               value;     /* kelvin-canonical temperature      */
    Mel_Thermal_Temp_Fidelity fidelity;  /* none | derived | measured (reused) */
} Mel_Thermal_Reading;
```

`fidelity == none` ⇒ `value == 0 K` (absolute-zero sentinel); ignore it and fall
back to the tier (MEL-ENGINE-VIII — honest absence, never a plausible lie).

## 3. `Mel_Thermal_Sensor` + enumeration

```c
typedef struct Mel_Thermal_Sensor Mel_Thermal_Sensor;
typedef Mel_Thermal_Reading (*Mel_Thermal_Sensor_Get)(Mel_Thermal_Sensor* self, void* user);

struct Mel_Thermal_Sensor {
    const char*             name;    /* SMC fourcc ("Tp01") or /sys zone type   */
    Mel_Thermal_Temp_Domain domain;  /* cpu | gpu | ambient | primary           */
    Mel_Thermal_Sensor_Get  get;     /* pull a fresh reading on demand          */
    u64                     handle;  /* opaque backend datum (see lowerings)    */
};

typedef struct { Mel_Thermal_Sensor* items; usize count; } Mel_Thermal_Sensor_List;

Mel_Thermal_Sensor_List mel_thermal_sensor_enumerate(const Mel_Alloc* alloc);
void                    mel_thermal_sensor_list_free(Mel_Thermal_Sensor_List*, const Mel_Alloc*);

static inline Mel_Thermal_Reading mel_thermal_sensor_read(Mel_Thermal_Sensor* self, void* user);
```

- **Allocator-passed** (MEL-CODE-003), **no fixed arrays** (MEL-CODE-002): the
  list is one contiguous allocation — `items[]` immediately followed by the
  name bytes, name pointers fixed up into that block. `*_list_free` is therefore
  a single `mel_dealloc`, platform-agnostic, defined once in `src/sensor.c`
  (`ALWAYS`).
- **Pull, not push** (MEL-ENGINE-III): `get` costs nothing until called; it
  re-reads its backing source each call. `user` is an opaque pass-through for the
  caller's context; `self->handle` is the backend's own datum.

`handle` per lowering: macOS = address of the process-lifetime static
`Mel_Smc_Sensor` (key+type+size) the read needs; Linux/Android = the
`thermal_zone<N>` index. Both are stable for the sensor's lifetime.

## 4. Per-platform lowerings (reuse, do not re-derive)

- **macOS** — enumerate the already-memoized SMC groups (`g_cpu/g_gpu/g_ambient`,
  built once by `mel_smc_init`): one sensor per classified `flt ` die key. `get`
  reads that single key; `measured` for cpu/gpu, `derived` for ambient (the
  module's standing ambient stance). Intel `sp78`-only Macs grouped nothing →
  empty, as the aggregate path already is.
- **Linux / Android** — enumerate `/sys/class/thermal/thermal_zone*` by scanning
  until the first absent `type`; classify by zone type (`gpu` → gpu,
  cpu/pkg/x86/soc/coretemp/acpitz → cpu, else primary). Each enumerated zone is a
  real kernel sensor → `measured`. Shared `mel_sysfs_sensor_enumerate` in
  `thermal_sysfs.h`.
- **iOS / Windows / Web** — empty list (no SMC in the iOS sandbox; WMI opt-in
  absent on Windows; no synchronous browser surface). Honest `{NULL, 0}`.

The aggregate `mel_thermal_temperature(domain)` stays; enumeration is additive.
`primary` is a virtual aggregate, not enumerated as a physical sensor.

## 5. Fallout — mpfr/gmp on every thermal platform (measured)

`thermal` previously cross-compiled clean on **all six** platforms with no
arbitrary-precision dependency. The `Mel_Real` choice makes `thermal` depend on
`temperature → math → mpfr → gmp`. Measured outcome after implementation:

- **macOS** (host) — builds and **runs**; full feature verified (§ below).
- **iOS, Android, Windows, Linux** — the build system auto-configured and built
  `mpfr`/`gmp` for each (fresh `third-party/{mpfr,gmp}/build/<plat>-debug/`);
  `libthermal.a` archives clean. No regression.
- **wasm** — **broken**: `./nob build thermal wasm` exits non-zero. The failure is
  in the third-party `gmp` autotools *install* step —
  `ranlib libgmp.a → LLVM ERROR: malformed uleb128, extends past end` (an
  llvm-ranlib / emscripten static-archive bug). The dependency build aborts there,
  before `mpfr` or any thermal wasm object compiles. The bug is **in the gmp wasm
  toolchain, not in this code** — but the `Mel_Real` backing is what now drags
  `thermal` onto that broken path. Before this change a wasm build of `thermal`
  had no reason to resolve gmp.

Blast radius is **contained**: no module currently includes `<thermal/thermal.h>`
or depends on `thermal` (the spec §8 consumers are aspirational), so nothing else
regresses from the new mpfr propagation.

Surfaced, not swallowed (MEL-ENGINE-VIII). Resolution is Gabbo's call (Rule #1):
(a) fix gmp's wasm `ranlib`/archive step (e.g. `RANLIB=emranlib` / `llvm-ar`
flags in the gmp wasm configure), (b) mark `thermal` `mel_unavailable` on wasm
while mpfr-backed, or (c) revisit the `Mel_Real` backing for portability. Tracked
in `modules/thermal/todo.md`.
