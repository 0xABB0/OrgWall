# 2026-05-31 — win32 cross-build from a macOS host

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
