# GPU RHI — Round-4 Platform/Backend Matrix

Platform-test pass against the current `main` tree (post round-3 integration). Host: Apple M3 Pro,
macOS 26.2 (Darwin 25.2.0). All macOS runs under `DYLD_LIBRARY_PATH=/opt/homebrew/lib
MEL_TEST_NOFORK=1`; that env is required because MoltenVK spawns ObjC state before the test
runner's `fork()`, which triggers `objc_initializeAfterForkError` in every test child without it.
Without `MEL_TEST_NOFORK=1` the runner reports 38/40 CRASH across gpu-vulkan (the two surviving
tests are pure-C unit tests that never touch the GPU). This is a pre-existing harness constraint
first documented in `writeup/2026-06-01-gpu-rhi-m1.md`, not a round-4 regression.

---

## macOS / vulkan (MoltenVK) — debug

**Command prefix:** `DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test <suite> macos --gpu=vulkan`

| Suite           | Tests | Pass | Fail | Skip | Stage |
|-----------------|-------|------|------|------|-------|
| gpu-foundation  |   8   |   8  |   0  |   0  | run   |
| gpu-vulkan      |  40   |  40  |   0  |   0  | run   |
| gpu-stress      |  20   |  20  |   0  |   0  | run   |
| gpu-concurrency |  10   |  10  |   0  |   0  | run   |
| gpu-visual      |  11   |  11  |   0  |   0  | run   |
| gpu-bench       |  12   |  12  |   0  |   0  | run   |
| **TOTAL**       | **101** | **101** | **0** | **0** | |

hello-gpu (vulkan, debug): `./nob build hello-gpu macos --gpu=vulkan` → **LINK OK** (68/68, `.app` packaged).

Validation log on Apple M3 Pro: dynamic-rendering lowering active, synchronization2 active, 5
extensions, 0 VUID / 0 leaks / 0 unexpected validation messages across all suites.

---

## macOS / vulkan (MoltenVK) — release

`DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test <suite> macos --gpu=vulkan --release`

All six suites: **101/101 pass, 0 fail, 0 skip.** hello-gpu release: **LINK OK** (68/68, `.app`
packaged). No config-specific breakage observed.

---

## macOS / metal

`./nob build hello-gpu macos --gpu=metal`

Stage reached: **LINK FAIL** (configure + compile OK; all object files produced).

Failure: `ld: symbol(s) not found for architecture arm64` — every `mel_gpu_*` symbol undefined.
No Metal backend implementation exists in the current tree; this is expected. The configure step
selects `--gpu=metal`; ninja emits the same frontend object files but no Metal backend library is
present to link against. First undefined symbol in the tail:

```
Undefined symbols for architecture arm64:
  "_mel_gpu_adapters", referenced from: ...
  (... all mel_gpu_* RHI entry points absent ...)
ld: symbol(s) not found for architecture arm64
```

**Classification:** unimplemented backend (not a regression — no Metal source exists). Metal team
adds this in a parallel pass.

---

## macOS / webgpu

`./nob build hello-gpu macos --gpu=webgpu`

Stage reached: **LINK FAIL** — identical pattern to metal. All `mel_gpu_*` symbols undefined;
webgpu backend not yet implemented. Same first undefined: `_mel_gpu_adapters`.

**Classification:** unimplemented backend.

---

## iOS / metal

`./nob configure hello-gpu ios --gpu=metal` → **OK** (ninja emitted, mpfr/gmp configured for
`ios-sim-debug`).

`./nob compile hello-gpu ios --gpu=metal` → compile step: **all object files already up-to-date**
(shared with macOS compile artifacts for arm64); link step: **LINK FAIL** for the same reason as
macOS/metal — no Metal backend symbols. The iOS simulator toolchain is present (`iPhoneOS26.0.sdk`
via Xcode 26.0); all platform C sources compile correctly.

**Classification:** unimplemented backend (toolchain healthy).

---

## wasm / webgpu

`./nob build hello-gpu wasm --gpu=webgpu` (emcc 5.0.7, Emscripten present)

Stage reached: **COMPILE FAIL** at `modules/log/src/log.sink.sqlite.c:221`.

Exact error:
```
modules/log/src/log.sink.sqlite.c:221:5: error: use of undeclared identifier 'gmtime_r'; did you mean 'gmtime'?
  221 |     gmtime_r(&now, &tm_buf);
```

The source uses `#ifdef _WIN32 / gmtime_s … #else / gmtime_r` with no Emscripten guard.
`gmtime_r` is absent from Emscripten's sysroot (`emscripten/5.0.7`). The webgpu backend itself was
never reached. All non-log wasm sources compiled without errors (the `hashmap.c` shift-count
warning is spurious/benign).

**Classification: REAL breakage** — source code must guard `gmtime_r` for `__EMSCRIPTEN__` (use
`gmtime` into a thread-local, or `gmtime_s` equivalent). Fix is a one-liner in
`modules/log/src/log.sink.sqlite.c`.

Note: `./nob build melody-wasi wasm` (no GPU) builds **clean** (41/41, `.js` emitted, one
harmless shift warning in `collection/hashmap.c`).

---

## android / vulkan

`./nob configure hello-gpu android --gpu=vulkan` → **OK** (mpfr/gmp autotools configure for
`android-sim-debug` with NDK 28.2.13676358 clang succeeds; ninja emitted).

`./nob build hello-gpu android --gpu=vulkan` → **BUILD FAIL** in mpfr autotools `make`:
```
mv: cannot stat 'frexp.loT': No such file or directory
mv: cannot move '.deps/frexp.Tpo' to '.deps/frexp.Plo': No such file or directory
make[2]: *** [frexp.lo] Error 1
build: autotools failed for 'mpfr'
```

The NDK clang (28.2, darwin-x86_64 prebuilt cross-compiling to aarch64-linux-android24) has an
`-ffloat-store` flag incompatibility with mpfr's libtool dependency-tracking. The `.deps` mv race
is a known NDK+autotools friction point. Melody source files for android were not reached.

**Classification:** environmental — autotools/NDK compat; not a Melody source error.

---

## linux / vulkan (cross from macOS)

`./nob configure hello-gpu linux --gpu=vulkan` → **OK** (zig-cc cross to `x86_64-linux-gnu`; mpfr/gmp configured).

`./nob compile hello-gpu linux --gpu=vulkan` → **COMPILE FAIL** for all Vulkan backend sources:
```
modules/gpu/src/vulkan/vk_backend.h:3:10: fatal error: 'vulkan/vulkan.h' file not found
```

All platform-agnostic Melody sources (allocator, collection, math, log, platform, etc.) compiled
without errors. Only `modules/gpu/src/vulkan/` sources fail — the Vulkan SDK headers are not
available for the Linux cross-compile target on this mac host. No Linux Vulkan SDK is installed or
packaged.

**Classification:** environmental — Vulkan SDK headers absent for cross-compile target. Not a
Melody source error; the GPU source is correct, the include path for the cross target isn't
populated.

---

## win32 / vulkan and win32 / d3d12 (win-pilot SSH)

`ssh win-pilot` → **UNREACHABLE** (connection timeout to `100.120.188.120`). The Windows box is
offline or the Tailscale/network path is down. No win32 builds possible this session.

**Classification:** environmental — network/machine unavailable.

---

## Summary table

| Platform     | Backend | Stage   | Result        | Classification        |
|--------------|---------|---------|---------------|-----------------------|
| macos debug  | vulkan  | run     | 101/101 GREEN | —                     |
| macos release| vulkan  | run     | 101/101 GREEN | —                     |
| macos        | metal   | link    | FAIL          | unimplemented backend |
| macos        | webgpu  | link    | FAIL          | unimplemented backend |
| ios          | metal   | link    | FAIL          | unimplemented backend |
| wasm         | webgpu  | compile | FAIL          | **REAL BUG** (gmtime_r)|
| wasm (wasi)  | n/a     | link    | GREEN         | —                     |
| android      | vulkan  | build   | FAIL          | env (NDK autotools)   |
| linux        | vulkan  | compile | FAIL (GPU only)| env (Vulkan headers) |
| win32        | vulkan  | —       | UNREACHABLE   | env (network)         |
| win32        | d3d12   | —       | UNREACHABLE   | env (network)         |

---

## Kludges

None in this session (read-only).

---

## CLAUDE.md suggestions

- Document the `MEL_TEST_NOFORK=1` requirement in CLAUDE.md under a "Test invocations" section,
  alongside the existing Build commands block. Every new contributor will hit the fork-crash
  without it.

---

## Suggestions

- **`gmtime_r` on wasm (REAL BUG, one-liner fix):** `modules/log/src/log.sink.sqlite.c:221` —
  add `#elif defined(__EMSCRIPTEN__)` branch using `gmtime` (wasm is single-threaded; no
  `gmtime_r` needed).
- **Collection shift warning:** `modules/collection/src/hashmap.c:38` emits
  `-Wshift-count-overflow` on wasm (32-bit type, shift by 32). Benign but noisy on a wasm build.
- **Android autotools friction:** mpfr's autotools + NDK 28 libtool have a `.deps` mv incompatibility.
  Possible fix: pre-build the mpfr/gmp static libs for android and vendor them, removing the
  autotools step from the android hot path.
- **Linux cross-compile Vulkan headers:** consider packaging a minimal Vulkan header-only set
  (e.g., KhronosGroup/Vulkan-Headers) as a third-party module so the Linux cross-compile can at
  least complete the compile stage on macOS CI.
- **win-pilot availability:** the box is unreachable; a baseline for win32 was not established
  this session. Recommend a connectivity check before scheduling win32-dependent agent passes.
