# temperature

A temperature **units** value-type, mirroring `frequency`. `Mel_Degrees`
wraps a `Mel_Real` (256-bit MPFR) held canonically in **kelvin**, and converts
exactly between the three principal scales — Celsius, Fahrenheit, Kelvin.

Standalone top-level module (peer of `math`/`color`), header-only — like `core`
and `easing` it ships no `.c`; every operation is `static inline` over
`math/real`, so consumers link `math`.

## Surface

```c
#include <temperature/temperature.h>

Mel_Degrees t = mel_degrees_celsius(36.6);     /* also _fahrenheit, _kelvin     */
double f      = mel_degrees_to_fahrenheit(t);  /* also _to_celsius, _to_kelvin  */

mpfr_t k; mel_degrees_view(k, &t);             /* exact kelvin view             */

Mel_Degrees delta = mel_degrees_sub(a, b);     /* add sub neg                   */
Mel_Degrees hot   = mel_degrees_max(a, b);     /* min max abs midpoint          */
double      ratio = mel_degrees_ratio(a, b);   /* thermodynamic (kelvin) ratio  */
uint8_t     eq    = mel_degrees_near(a, b, tol);
```

## Why kelvin canonical

- SI base unit; absolute zero is `0`, so `mel_degrees_is_absolute_zero` is an
  unambiguous sentinel that never collides with a real `0 °C` (`= 273.15 K`).
- Multiplicative / ratio operations are physically meaningful only on an
  absolute scale.
- Celsius⇄Kelvin reduces to an exact rational add.

## Why exact

Every conversion is expressed through `mel_real`'s exact-rational adds and
`mul_ui`/`div_ui` — never a lossy `double` scale factor:

```
°C → K : C + 27315/100          °F → K : (F + 45967/100) · 5/9
K → °C : K − 27315/100          K → °F : K · 9/5 − 45967/100
```

This exactness is the reason the `Mel_Real` backing earns its weight; a plain
`f64` would suffice numerically but would not mirror `frequency`.

The music-domain operations of `frequency` (`transpose_*`, `octave_*`,
`harmonic`, `mod`, `floordiv`, `beat`) are intentionally absent — they carry no
temperature meaning.

## Dependencies

`core` (compiler attributes) and `math` (`Mel_Real`, and through it `mpfr`/`gmp`).
Anything depending on `temperature` inherits the `mpfr`/`gmp` link.
