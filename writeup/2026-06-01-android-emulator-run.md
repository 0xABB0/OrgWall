# Android emulator run target

## Work done

`./nob run <app> android` now boots an AVD automatically when none is online, so a
physical device need no longer stay attached.

- `resolve.c` — Android's native variant now defaults `simulator = true`, mirroring iOS.
  This makes the emulator the default run target; `--device` (already parsed in
  `driver.c`, flipping `v.simulator`) selects a physical device.
- `driver.c` — added `android_emulator_bin` (resolves `$ANDROID_HOME/emulator/emulator`,
  falling back to PATH), `android_emulator_serial` (reads the first online
  `emulator-*` serial from `adb devices`), and `android_boot_emu` (reuses a running
  emulator, else launches `emulator -avd <name>` detached and blocks on a device-side
  `getprop sys.boot_completed` loop). The Android branch of `launch()` calls it under
  `v->simulator`, then threads `-s <serial>` through `adb install` / `am start` so the
  emulator is targeted unambiguously even when a device is also attached. The AVD name
  comes from the target manifest key `AVD`, else the first `emulator -list-avds` entry.

The emulator/device split is launch-only, not a compile axis: no `Mel_When` selector and
no `build.c` keys off `simulator`, so the only build-side effect is the `-sim` outdir
suffix (`mel_target_outdir`). The APK is identical; package step and launch read the same
`android-sim-debug/` directory.

Validated by the unity build (`clang -std=c23 -g -Imodules/build -o nob nob.c`); compiles
clean and the binary runs.

## Kludges

- The `-sim` outdir suffix now applies to Android even though emulator and device consume
  a byte-identical APK (same arm64 ABI). Switching device↔emulator forces a rebuild into a
  separate directory for no compile difference. Accepted to keep symmetry with iOS's
  `simulator` axis; the alternative was a launch-only flag that left the outdir alone.
- `package.c:273` still hardcodes `jniLibs/arm64-v8a`. Correct for an arm64-v8a system
  image on Apple-Silicon hosts; an x86_64 emulator (Intel host) would also need
  `--arch=x86_64` *and* that ABI directory parametrized by `v->arch`. Out of scope here.
- `./nob debug <app> android` still runs a bare `adb logcat` with no `-s` and no auto-boot;
  with the emulator default it will block if nothing is running. Left untouched — the ask
  was the run path.
- `android_boot_emu` boots via `system()` with a shell `&` and a device-side poll loop
  rather than a host timer, and leaks a few short-lived `mel_str_fmt` buffers on its error
  paths, consistent with the surrounding driver code.

## CLAUDE.md suggestions

None.

## Suggestions

- If the `-sim` outdir fork proves wasteful, special-case `mel_target_outdir` to omit the
  suffix for Android (emulator-ness is not a build axis there) while keeping the launch
  behavior.
- A `--avd <name>` CLI flag would let a session pick an AVD without a manifest `AVD` key.
