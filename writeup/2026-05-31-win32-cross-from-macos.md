# 2026-05-31 — win32 cross-build from a macOS host

> **Addendum (same session):** the two debts confessed below — the blunt `-std=gnu17`
> pin and the console-only PE — were subsequently *fixed* as framework capabilities.
> See "## Follow-up: subsystem control & per-library C standard" at the foot of this file.
> The original account is kept verbatim for the record.

## Work done

`./nob build <gui-app> win32` failed before reaching any Melody code. Two distinct
layers were broken; both are fixed, and the chain now produces a `PE32+` `.exe`.

### 1. GMP/MPFR autotools configure aborted (`modules/build/toolchain.c`)

The win32 third-party path cross-compiles GMP 6.3.0 and MPFR via autotools, using
`tc.autotools_cc` (`x86_64-w64-mingw32-gcc`, Homebrew, **GCC 15.2.0**). GMP's configure
ran its *"long long reliability test 1"* conftest, which declares `void g(){}` and calls
it with six arguments — legal K&R-style under C17, where empty `()` means "unspecified
parameters". GCC 15 defaults to **`-std=gnu23`**, and in C23 empty `()` means `(void)`, so
the call is a hard error (`too many arguments to function 'g'`). configure concluded
*"could not find a working compiler"* and `build_autotools` returned false → `build:
autotools failed for 'gmp'`.

Fix: pin the autotools cross compiler's language to the era the conftests assume —
`tc.autotools_cc = "<arch>-w64-mingw32-gcc -std=gnu17"`. Verified directly: the extracted
conftest exits 1 under the C23 default and exits 0 under both `gnu17` and `gnu11`. After the
fix, both libraries configured (`configure: exit 0`) and installed `libgmp.a` (1.0 MB) and
`libmpfr.a` (3.4 MB) into their win32 prefixes; `musictheory` (the transitive consumer via
`math → mpfr → gmp`) then compiled.

Scope: the flag rides only on `tc.autotools_cc`, which is consumed solely by the cross
autotools branch (`thirdparty.c:114`). It does not touch `tc.cc` (zig), so the Melody TUs
still build at `-std=c23`. macOS native autotools is unaffected (`tc.cross` is false there,
so no `CC=` override is emitted at all).

### 2. Missing Windows import libraries at final link (`modules/gui/build.c`, `modules/midi/build.c`)

With the third-party libraries built, the link advanced to the executable and failed on
undefined OS symbols — no `-l` flags for the system DLLs the winui/win32 backends call.
Mapped each symbol to its import library by scanning the sources:

- `SetWindowSubclass`, `RemoveWindowSubclass`, `DefSubclassProc`, `InitCommonControlsEx`
  → **comctl32** (common-controls subclassing, `<commctrl.h>`).
- `SetBkMode`, `SelectObject`, `GetStockObject` → **gdi32**.
- `midiInGetNumDevs`, `midiInGetDevCapsA`, `midiInOpen`, `midiInStart`, `midiInStop`,
  `midiInReset`, `midiInClose` → **winmm** (`<mmsystem.h>`).

Fix: `mel_link(gui, MEL_PUBLIC, WHEN(WIN32), "-lcomctl32", "-lgdi32")` and
`mel_link(midi, MEL_PUBLIC, WHEN(WIN32), "-lwinmm")`, sited beside each module's existing
macOS `-framework` lines. `MEL_PUBLIC` so the flags propagate to the app along the dep
graph (mirroring `rng`'s existing `-lbcrypt` precedent). Only libraries whose symbols
actually appeared were added — no speculative uxtheme/ole32/dwmapi.

## Verification

- `midi-monitor` win32 → `midi-monitor.exe`, `PE32+ executable (console) x86-64` — full
  chain gui + midi + musictheory → mpfr → gmp.
- `hello-world-gui` win32 → `.exe`, `PE32+` (exercises the gui fix without midi).
- `hello-world-gui` macos → links and packages `.app` — confirms the toolchain change is
  win32-scoped and didn't regress the host.

## Kludges (MEL-ENGINE-VIII — full account)

- **`-std=gnu17` is a blunt pin on the whole autotools cross CC, not a targeted conftest
  patch.** It fixes the symptom (GCC's default language) rather than the root rot (GMP
  6.3.0's conftest is not C23-clean). Consequences: (a) if a *future* vendored autotools lib
  genuinely needs C23 to configure, this denies it — at which point the flag should move from
  the global toolchain onto a per-target `mel_configure` option; (b) it only constrains
  *configure-time* conftests and GMP's own build — it does not, and should not, change how the
  Melody TUs compile. I judged the blunt pin acceptable because every autotools dep we vendor
  (gmp, mpfr) is old-standard C and benefits identically. Flagged, not hidden.
- **Console subsystem, not GUI.** The produced `.exe` is a *console* PE (the link has no
  `-Wl,--subsystem,windows`/`-mwindows`), so a GUI app launched from Explorer will pop a
  console window. Out of scope for "make it build," but it is debt against a shippable GUI
  binary. Not addressed.
- **Not executed.** Correctness is asserted from successful linking and PE headers only; no
  win32 binary was run (no wine on this host, and a PE can't exec on aarch64-darwin — the
  configure conftests are compile/link-only, and `make check` is never invoked). Runtime
  behaviour on real Windows is unverified by me.
- **`.exe` is unstripped/debug.** Expected for a debug build; noted for completeness.

## CLAUDE.md / platforms.md suggestions (recommendations only — not applied)

- `modules/build/platforms.md` "Known gaps" currently states *"win32/wasm GUI link is
  blocked on `gmp` cross issues."* That sentence is now stale for win32 and should be
  retired (wasm GUI is untouched and remains blocked).
- The same doc's win32 toolchain line should note the `-std=gnu17` pin on the autotools CC
  and *why* (mingw GCC 15 C23 default vs. GMP/MPFR pre-C23 conftests), so the next person
  who bumps the compiler or vendors another autotools lib understands the constraint.

## Suggestions

- **Verify on real Windows.** The natural next step you've taken (win32 *host*) is also the
  only way to close the "not executed" gap above — a CI smoke-run of the `.exe` would catch
  the console-subsystem and any runtime-link issues this static check can't.
- **Consider a `--subsystem` knob** (or auto-selecting windows-subsystem for GUI app targets
  vs console for CLI targets) so GUI `.exe`s don't spawn a console. This is a packaging-layer
  decision, orthogonal to the cross fix.
- **Pre-flight the cross toolchains.** A one-line `mel_toolchain` probe ("is
  `<triple>-gcc` present, does a trivial conftest link?") emitted as a clear, early error
  would turn opaque autotools `config.log` archaeology into an immediate, honorable failure
  (MEL-ENGINE-VIII).

---

## Follow-up: subsystem control & per-library C standard

Both debts from the original account are now retired as first-class framework
features, not patches. New build-API surface in `modules/build/build.h`:

- `mel_subsystem(t, "console" | "gui")` — per-target PE subsystem; default console.
- `mel_configure_cstd(t, "<std>")` — per-third-party C standard for the autotools CC.

### Per-library C standard (replaces the blunt pin)

`tc.autotools_cc` is back to the bare cross compiler (`<arch>-w64-mingw32-gcc`); the
toolchain no longer carries a language pin. The C era a third-party autotools library
needs is now declared *by that library*: `gmp/build.c` and `mpfr/build.c` each call
`mel_configure_cstd(t, "gnu17")`. `thirdparty.c` folds it onto the cross `CC`
(`CC=<cc> -std=gnu17`) — deliberately onto **CC, not CFLAGS**, because GMP's configure
chooses its own CFLAGS for the ABI conftests and ignores the user's, so a CFLAGS-borne
`-std` would never reach the failing "long long reliability test." The flag rides CC,
which every conftest invokes.

Why this is no longer blunt (MEL-ENGINE-IV — constrain conventions, not capabilities):
the standard is now a per-target property with no global default, so a future win32
autotools library written in C23 simply omits the call (or passes `c23`) and is no longer
denied. The win32 toolchain stays clean; only libraries that declare a need get the flag.

Verified: `rm -rf third-party/{gmp,mpfr}/build/win32-debug` then a from-clean win32 build
reproduced `libgmp.a`/`libmpfr.a`; `config.log` records
`CC='x86_64-w64-mingw32-gcc -std=gnu17'`, zero "could not find a working compiler", and the
"long long reliability test 1" failure marker is gone.

### Subsystem (fixes the console PE)

`mel_subsystem` stores a string on the target; `mel_subsystem` itself validates the value
and `abort()`s on anything but `console`/`gui` (MEL-ENGINE-VIII — no silent
mis-selection). `emit.c`, for a win32 executable whose subsystem is `gui`, appends a single
flag — `-Wl,--subsystem,windows` — to the link. No explicit entry-point override is
emitted: lld-link infers the entry from which `main`-family symbol is defined, so with the
user's `int main` present it selects `mainCRTStartup` while still honoring the forced
windowed subsystem. This is also why no `WinMain` shim is needed and why `-mwindows` (which
would drag GUI link libs) is avoided. Console is the default (no flags) — correct for the
framework's CLI and daemon targets.

The property is **orthogonal to the gui module dependency**: a gui-dependent target may
stay `console` (a GUI app that wants a console — `midi-monitor` is left exactly this way as
the worked example), and any target may opt into `gui`. The four windowed sample apps
(`hello-world-gui`, `barcode-gui`, `display-gui`, `hello-gpu`) now declare
`mel_subsystem(app, "gui")`.

I probed four link incantations with `zig cc -target x86_64-windows-gnu` before choosing:
default → console PE; `--subsystem,windows` alone → GUI PE, links a plain `int main`
cleanly (**chosen**); `--subsystem,windows -Wl,-emainCRTStartup` → **rejected** by zig's
lld-link (`unsupported linker arg: -emainCRTStartup`), so the explicit entry override is
both unavailable and unnecessary; `-mwindows` → silently ignored, stays console. The
winning option needs no entry flag because lld-link's symbol-based entry inference already
picks `mainCRTStartup` for us.

Verified: `hello-world-gui` win32 → `PE32+ executable (GUI)`; `midi-monitor` win32 →
`PE32+ executable (console)`; both link exit 0. macOS regression: `hello-world-gui` macos
links and packages its `.app` (exit 0) — the subsystem block is win32-gated and host-inert.

### Kludges / debt (MEL-ENGINE-VIII)

- **`cstd` is applied only on the cross CC path.** A *host* autotools build (e.g. gmp on
  macOS) ignores `autotools_cstd` because we never set `CC` for non-cross configures (we
  let configure detect it). Harmless today — every host compiler we use (clang) already
  defaults to gnu17 — but if a future host compiler defaults to C23, a host autotools build
  could regress and the per-library declaration would not save it. Documented, not hidden;
  the fix when needed is to also emit `CC=<host cc> -std=<cstd>` for non-cross.
- **GUI `.exe`s were not executed.** Subsystem correctness is asserted from the PE header
  (`file` reports GUI vs console) and a clean link; no binary was run (no wine, can't exec
  PE on aarch64-darwin). Whether a GUI-subsystem `main` runs without a console at runtime,
  and whether lld-link's entry inference does pick `mainCRTStartup` at runtime rather than
  just at link, is unverified by me — the natural check on a Windows host.
- **`clang-format` is not installed on this host**, so I matched the surrounding style by
  hand and could not mechanically confirm zero drift on the touched build files. A format
  pass once the tool is available would close this.

### platforms.md recommendation (not applied — recap is recommendation-only)

`mel_subsystem` and `mel_configure_cstd` should be added to the authoring surface in
`modules/build/platforms.md` (the "Authoring a build.c" and "Third-party" sections), and
the "Known gaps" bullet about the win32 console PE / gmp pin retired.
