# frequency

A frequency **units** value-type. `Mel_Hz` wraps a `Mel_Real` (256-bit MPFR)
held canonically in **hertz**, exact under rational transposition so a pitch
chain never accumulates `double` drift.

Standalone top-level module (peer of `math`/`temperature`). Most operations are
`static inline` over `math/real`; the two irrational transpositions
(`transpose_cents`, `transpose_semitones`, which raise `2` to a fractional
power) live in `src/frequency.c`, so consumers link `frequency`.

## Surface

```c
#include <frequency/frequency.h>

Mel_Hz a = mel_freq(440.0);          /* also mpfr_t, mpq_t, or num/den rational */
double  v = mel_freq_to_double(a);
mpfr_t  x; mel_freq_view(x, &a);      /* exact hertz view                        */

Mel_Hz sum   = mel_freq_add(a, b);    /* add sub neg mul div                     */
Mel_Hz oct   = mel_freq_octave_up(a); /* octave_down harmonic transpose          */
Mel_Hz up    = mel_freq_transpose_cents(a, cents);   /* and _semitones           */
Mel_Hz beat  = mel_freq_beat(a, b);   /* |a − b|, the beating frequency          */
double ratio = mel_freq_ratio(a, b);  /* min max abs midpoint mod floordiv       */
uint8_t eq   = mel_freq_near(a, b, tol);
```

## Why exact

Octaves, harmonics, and just-intonation transposition are exact rational
operations on `mel_real` (`mul_ui`/`div_ui`/`mul_q`) — never a lossy `double`
factor — so a long transposition chain stays precise. Only the equal-tempered
`transpose_cents`/`_semitones` are irrational (`2^(n/1200)`, `2^(n/12)`) and
round once through `mpfr`.

## Dependencies

`core` (compiler attributes) and `math` (`Mel_Real`, and through it `mpfr`/`gmp`).
Anything depending on `frequency` inherits the `mpfr`/`gmp` link.
