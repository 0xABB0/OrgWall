# cpu

Static CPU topology and cache geometry — the same surface FlaxEngine's `CPUInfo`
publishes, lowered per platform by source-directory gating (the platform is a
build axis, not a runtime handle).

## Surface

One pull-only read; global, stateless, no instance:

```c
#include <cpu/cpu.h>

typedef struct
{
    u32 package_count;    /* physical processor packages (sockets) */
    u32 core_count;       /* physical cores                        */
    u32 logical_count;    /* logical processors (SMT/HT included)  */
    u32 l1_cache_size;    /* bytes — one L1 data cache             */
    u32 l2_cache_size;    /* bytes — one L2 cache                  */
    u32 l3_cache_size;    /* bytes — one L3 cache                  */
    u32 page_size;        /* bytes                                 */
    u64 clock_speed;      /* Hz                                    */
    u32 cache_line_size;  /* bytes                                 */
} Mel_Cpu_Info;

Mel_Cpu_Info mel_cpu_info(const Mel_Alloc* alloc);
```

Each field's zero value is the honest absence: a field reads `0` when the build's
platform publishes no source for it. The engine never fabricates a count, a size,
or a clock. Cache sizes are the size of a *single* cache instance (the per-core L1
data cache, one L2, one L3), matching Flax's semantics — not an aggregate.

## Allocator

`alloc` is transient scratch for the one backend whose OS surface demands a
variable-length buffer: Windows' `GetLogicalProcessorInformationEx`. It is
acquired and released within the call; nothing in the returned struct points into
it. The macOS/iOS, Linux/Android and web backends ignore `alloc`. It is a required
argument on every platform so the signature is uniform and the one real cost stays
explicit and traceable (MEL-ENGINE-III); pass `mel_alloc_heap()` if you have no
preference. `alloc` must be non-NULL.

## Lowering

```
modules/cpu/
  readme.md  build.c
  include/cpu/cpu.h
  src/
    apple/cpu.c   (macOS + iOS: sysctlbyname hw.*)
    linux/cpu.c   (Linux + Android: sysfs topology + cache, sysconf, cpufreq)
    win32/cpu.c   (GetLogicalProcessorInformationEx + GetSystemInfo + registry ~MHz)
    web/cpu.c     (navigator.hardwareConcurrency; wasm page granularity)
  test/cpu_test.c (host: print + invariant checks; a mel_add_test target)
```

- **apple** — `sysctlbyname` over `hw.{packages,physicalcpu,logicalcpu,l1dcachesize,
  l2cachesize,l3cachesize,pagesize,cpufrequency,cachelinesize}`. On Apple Silicon
  `hw.cpufrequency` and `hw.l3cachesize` are absent, so `clock_speed`/`l3_cache_size`
  honestly read `0`; Intel Macs report both. No Objective-C — compiled as plain C.
- **linux / android** — `package_count`/`core_count` counted by the topology
  *leader* test: a logical CPU is counted iff it is the lowest-numbered entry in its
  `package_cpus_list` / `core_cpus_list` sibling set (legacy `core_siblings_list` /
  `thread_siblings_list` fallback). This is exact for sparse CPU ids and needs no
  storage — no fixed arrays, no heap. Caches and line size come from
  `cpu0/cache/index*`; `logical_count`/`page_size` from `sysconf`; `clock_speed`
  from `cpu0/cpufreq/cpuinfo_max_freq` (kHz→Hz), falling back to `/proc/cpuinfo`
  `cpu MHz`. Where `/sys` topology is unreadable (some containers) the counts stay
  `0` while `logical_count` still resolves. Android reuses this lowering verbatim —
  no JNI.
- **win32** — `RelationProcessorPackage`/`RelationProcessorCore`/`RelationCache`
  records from `GetLogicalProcessorInformationEx(RelationAll, …)`; `page_size` from
  `GetSystemInfo`; `logical_count` from `GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)`
  (correct past 64 logical processors); `clock_speed` from the registry `~MHz`
  (MHz→Hz). Links `-ladvapi32` for `RegGetValueA`.
- **web** — only `navigator.hardwareConcurrency` is synchronously available, so
  `logical_count` is filled and everything else stays `0`, except `page_size`, set
  to the WASM linear-memory page granularity (65536 B). No host CPU geometry is
  exposed by the runtime.

## Status

- **macOS** — built and run on the host (M-series): physical/logical counts, L1d/L2,
  page size, cache line filled; `l3_cache_size` and `clock_speed` honestly `0` on
  Apple Silicon. Test passes.
- **Linux / Android / Windows / Web** — implemented against the documented OS
  surfaces; not run here. Windows and Linux cross-compile through the configured
  toolchains.
