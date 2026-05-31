# Melody Temperature — `temperature`

A temperature units value-type, the thermal-domain analogue of `frequency`.
It exists so that `thermal` (and any other consumer) can carry a temperature as a
unit-correct quantity rather than a bare `float` whose scale the reader must
guess.

Bound by the Ten Commandments of the Engine; cited by tag where a decision turns
on one.

---

## 1. Identity

Standalone top-level module at `modules/temperature/`, a peer of `math` — **not** a
sub-namespace of `thermal`. Rationale: a unit is not owned by one consumer. Like
`frequency` — itself a standalone peer, not nested in any one consumer — temperature is reachable to any
module (media encoders, power thermal-budget logic, UI read-outs) without forcing
a dependency on `thermal`'s platform telemetry. The cost is one explicit
`thermal → temperature` edge.

Header-only (`core`/`easing` precedent): no `.c`, all `static inline` over
`math/real`. Consumers therefore link `math`, and through it `mpfr`/`gmp`.

## 2. Value type

```c
typedef struct { Mel_Real v; } Mel_Degrees;   /* canonical: kelvin */
```

`Mel_Real` is the faithful mirror of `Mel_Hz`'s backing (MEL-ENGINE-IX — the
units family shares one numeric core). **Kelvin** is canonical because:

- it is the SI base scale;
- absolute zero `= 0` yields an unambiguous sentinel
  (`mel_degrees_is_absolute_zero`) that never aliases a genuine `0 °C`
  (`= 273.15 K`) — important for the `thermal` `none`-reading sentinel;
- ratio and scalar-multiply are physically meaningful only on an absolute scale.

## 3. Conversions — exact, three scales

The "three main units": Celsius, Fahrenheit, Kelvin. Constructors take any scale;
accessors return any scale. Conversions never use a lossy `double` factor — they
ride `mel_real`'s exact rationals and integer `mul_ui`/`div_ui`:

| from → canonical | canonical → to |
| --- | --- |
| `°C → K = C + 27315/100`        | `K → °C = K − 27315/100` |
| `°F → K = (F + 45967/100)·5/9`  | `K → °F = K·9/5 − 45967/100` |
| `K → K = K`                     | `K → K = K` |

Exactness is the justification for the `Mel_Real` weight (MEL-ENGINE-II — the
power is real, not decorative). A consumer that only needs `float` precision pays
nothing extra at the call site: the conversion accessors return `double`.

## 4. Operations

Mirrors the *general-purpose* subset of `frequency`:

- construct: `mel_degrees_{celsius,fahrenheit,kelvin}` (`double` / `mpfr_srcptr`)
- read: `mel_degrees_to_{celsius,fahrenheit,kelvin}` → `double`; `mel_degrees_view`
  → exact kelvin `mpfr_t`
- arithmetic: `add`, `sub`, `neg`, `mul`/`div` (by `double` or `mpfr_srcptr`),
  `ratio`
- reduce: `abs`, `min`, `max`, `midpoint`
- compare: `cmp`, `eq`, `near(tolerance)`, `is_absolute_zero`

The music-only operations of `frequency` (`transpose_*`, `octave_*`,
`harmonic`, `mod`, `floordiv`, `beat`) are **omitted** — they have no temperature
semantics (MEL-ENGINE-IX — compose meaningful parts, do not bolt on a special
case). `add`/`neg` on an absolute scale are unphysical but retained as mechanism
(MEL-ENGINE-V — the engine reports/offers, the consumer decides meaning); `sub`
of two temperatures yields a delta in kelvin/celsius magnitude.

## 5. Dependencies

`core`, `math`. No platform axis — pure value arithmetic, identical on every
build.
