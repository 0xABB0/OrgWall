# 2026-06-02 — `cpu` module (CPUInfo parity)

## Work done

New top-level module `modules/cpu/` exposing the same static CPU facts as
FlaxEngine's `Source/Engine/Platform/CPUInfo.h`: package / physical-core /
logical-processor counts, L1/L2/L3 cache sizes, page size, clock speed (Hz), and
cache-line size — one flat `Mel_Cpu_Info` struct, one pull-only query.

```c
Mel_Cpu_Info mel_cpu_info(const Mel_Alloc* alloc);
```

Modelled on the `power`/`thermal` sibling pair: platform is a build axis, the
backend chosen by source-directory gating in `build.c`, no runtime handle. No
enums (none warranted — all fields are scalar counts/sizes, MEL-CODE-001), no
fixed arrays (MEL-CODE-002), no comments, honest-absence semantics (a field reads
`0` where its platform publishes no source — MEL-ENGINE-VIII, MEL-CODE-007).

Backends:
- `src/apple/cpu.c` — `sysctlbyname` over `hw.*`; plain C (no Objective-C).
- `src/linux/cpu.c` — sysfs topology *leader* counting (allocation-free, exact for
  sparse ids), `cpu0/cache/index*`, `sysconf`, `cpufreq`/`proc` clock. Shared
  verbatim with Android (no JNI).
- `src/win32/cpu.c` — `GetLogicalProcessorInformationEx(RelationAll)` +
  `GetSystemInfo` + `GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)` + registry
  `~MHz`. Links `-ladvapi32`.
- `src/web/cpu.c` — `emscripten_num_logical_cores()` + wasm page granularity.

`test/cpu_test.c` is a `mel_add_test` target (`nob test cpu-test`): prints every
field and asserts `logical_count > 0`, `page_size > 0`, `logical_count >=
core_count`.

Verification:
- **macOS** (M-series host) — built + run + test green. `cores == logical == 12`
  (no SMT on Apple Silicon), L1d 65536, L2 4 MiB, page 16384, cache line 128;
  `l3_cache_size == 0` and `clock_speed == 0` are the honest absences on arm64.
- **Linux** — clean cross-compile through the build system's `zig cc` toolchain.
- **win32** — backend compiles clean against zig's bundled mingw `windows.h`
  (the build system's *local* win32 path is broken host-wide; see Kludges). Not run.
- **wasm** — compiles under `-std=c23` and links+runs under node
  (`logical=1 page=65536`); `emscripten_num_logical_cores` resolves without
  `-pthread`. Not browser-verified.

## Kludges (full account — MEL-ENGINE-VIII)

- **Allocator unused on 4 of 5 backends.** Only win32 needs it (the
  `GetLogicalProcessorInformationEx` buffer is variable-length). The argument is
  required uniformly so the signature is one shape and the single real allocation
  stays explicit (MEL-ENGINE-III) — but apple/linux/web `MEL_UNUSED` it. A caller
  on those platforms pays one ignored argument. Deliberate tradeoff, recorded as a
  cost, not hidden.
- **L1 is the L1 *data* cache only.** Flax's single `L1CacheSize` is ambiguous
  (separate L1i/L1d on every modern core). I report L1d (apple `hw.l1dcachesize`;
  linux first `Data`/`Unified` level-1; win32 `CacheData`/`CacheUnified`). L1i is
  dropped silently — documented in the readme but the struct can't express it.
- **Linux `clock_speed` is nominal max, and x86-biased on the fallback.** Primary
  source is `cpuinfo_max_freq` (the governor's max, not current). Fallback is
  `/proc/cpuinfo` `cpu MHz`, which ARM kernels do not emit — so ARM Linux without a
  cpufreq sysfs node yields `0`. Honest absence, but a real coverage hole on ARM.
- **win32 / android not run.** win32 is validated only by compiling against mingw
  headers; the repo's *local* win32 build emits plain `clang` with no `-target` and
  fails on `windows.h` — reproduced identically with the pre-existing `power`
  module, so it is a host/toolchain gap, not this module's bug. Real win32
  validation must happen on the `win-pilot` box. Android shares the Linux TU
  (compile-verified there) but ran on no device.
- **`EM_ASM_INT` → `emscripten_num_logical_cores` swap.** First draft used
  `EM_ASM_INT(navigator.hardwareConcurrency)`; emscripten rejects `EM_ASM` under
  `-std=c*`, and the build forces `-std=c23` on every TU. Switched to the plain
  extern. Caught only because I compiled with `emcc` — the macOS host build would
  never have surfaced it.
- **web `page_size = 65536` is the wasm linear-memory page**, an engine/VM
  granularity, not a host CPU MMU page. Presented under the same field as the other
  platforms' real page size. Documented; mild semantic stretch.
- **No `package`/`core` counts on topology-less Linux.** Containers that hide
  `/sys/devices/system/cpu/*/topology` yield `package_count == core_count == 0`
  while `logical_count` still resolves from `sysconf`. Honest, but partial.

## CLAUDE.md suggestions (recommendations only — not applied)

- The build doc claims win32 cross-compiles via `zig cc -target
  x86_64-windows-gnu`, but on this macOS host the emitter produces bare `clang` and
  every win32 TU fails at `windows.h` (verified with `power`). Either the toolchain
  resolution regressed or the doc overstates local support. Worth a note in
  `modules/build/platforms.md` "Known gaps" that local win32 builds currently
  require the remote box.
- `clang-format` is not on PATH (only `/opt/homebrew/opt/llvm/bin/clang-format`).
  A `./nob fmt` verb or a documented formatter path would prevent silent
  non-formatting.

## Suggestions

- **Cache topology is richer than one number per level.** A future `cpu` revision
  could expose L1i separately and per-cache `shared_cpu` extent (how many logical
  CPUs share each cache) — the win32 `CACHE_RELATIONSHIP` and linux
  `shared_cpu_list` already carry it; the struct just doesn't.
- **`thermal`/`power`/`cpu` now form a coherent "host telemetry" cluster** under
  the same build-axis pattern. If a fourth lands (memory: total/available/page
  stats), consider whether they want a shared `readme` cross-link rather than three
  parallel modules with copied prose.
- The `mel_add_test` harness has no aggregate runner (per `platforms.md`); `cpu-test`
  is a self-contained `main` like the `continuation` drivers. A real registry-backed
  runner in the `test` module would let this use `MEL_TEST`/`MEL_EXPECT_*`.
