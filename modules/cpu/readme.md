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

Mel_Cpu_Info mel_cpu_info(void);
```

Each field's zero value is the honest absence: a field reads `0` when the build's
platform publishes no source for it. The engine never fabricates a count, a size,
or a clock. Cache sizes are the size of a *single* cache instance (the per-core L1
data cache, one L2, one L3), matching Flax's semantics — not an aggregate.

## No heap

`mel_cpu_info` allocates nothing on the heap on any platform; the whole result is
returned by value and every backend's scratch lives on the stack. The one OS surface
that demands a variable-length buffer — Windows' `GetLogicalProcessorInformationEx` —
is served by `_alloca` of the size the API itself reports, released when the call
returns. macOS/iOS, Linux/Android and web need no buffer at all. The struct never
points into any of this scratch.

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
  records from `GetLogicalProcessorInformationEx(RelationAll, …)`, walked in a
  `_alloca`'d stack buffer sized to the length the first probe call reports — no heap;
  `page_size` from `GetSystemInfo`; `logical_count` from
  `GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)` (correct past 64 logical processors);
  `clock_speed` from the registry `~MHz` (MHz→Hz). Links `-ladvapi32` for `RegGetValueA`.
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

## Runtime capabilities

Beyond the static topology, the module publishes a runtime capability snapshot —
SIMD instruction-set presence, total physical RAM, and the SIMD alignment the
detected ISA wants — plus an allocator-routed aligned allocation helper.

```c
#include <cpu/cpu.h>

typedef u64 Mel_Cpu_Features;   /* flag bitset, not a closed enum */

enum
{
    MEL_CPU_FEATURE_SSE, MEL_CPU_FEATURE_SSE2, MEL_CPU_FEATURE_SSE3,
    MEL_CPU_FEATURE_SSSE3, MEL_CPU_FEATURE_SSE41, MEL_CPU_FEATURE_SSE42,
    MEL_CPU_FEATURE_AVX, MEL_CPU_FEATURE_AVX2, MEL_CPU_FEATURE_AVX512F,
    MEL_CPU_FEATURE_NEON,   /* bit positions; see header for the 1<<n values */
};

typedef struct
{
    Mel_Cpu_Features features;     /* detected, then masked                */
    u64              ram_total;    /* bytes of physical RAM                */
    u32              simd_align;   /* 16 / 32 / 64; 0 when no SIMD          */
} Mel_Cpu_Caps;

Mel_Cpu_Caps     mel_cpu_caps(void);
bool             mel_cpu_has(Mel_Cpu_Features set, Mel_Cpu_Features want);
u64              mel_cpu_ram_total(void);
u32              mel_cpu_simd_align(void);

void             mel_cpu_feature_mask_set(Mel_Cpu_Features allowed);
Mel_Cpu_Features mel_cpu_feature_mask_get(void);

void* mel_cpu_simd_alloc(const Mel_Alloc* alloc, usize size, u32 align);
void  mel_cpu_simd_dealloc(const Mel_Alloc* alloc, void* ptr, u32 align);
```

- **honest absence** — an absent ISA reads `false`; `ram_total` reads `0` where the
  runtime exposes no figure (web); `simd_align` reads `0` when no SIMD feature is
  present. No count, size, or capability is fabricated.
- **compat mask** — `mel_cpu_feature_mask_set` AND-clamps the *reported* feature set
  (and hence `simd_align`), to exercise narrower-ISA code paths on capable hardware.
  Its identity is allow-all (`~0`); detection underneath is never altered.
- **alignment** — `simd_align` is the widest detected lane's requirement: `64` for
  AVX-512F, `32` for AVX/AVX2, `16` for any SSE level or NEON.
- **aligned alloc** — `mel_cpu_simd_alloc` routes through the supplied `Mel_Alloc`
  (MEL-CODE-003); it never touches `malloc`. `align` must be a power of two.

### Feature lowering

- **x86 (any OS)** — `CPUID` leaves 1 and 7, with `XGETBV(XCR0)` gating AVX/AVX2 on
  OS-saved YMM state and AVX-512F on ZMM/opmask state, so a feature the OS will not
  preserve is honestly not reported.
- **apple ARM** — `sysctlbyname hw.optional.{neon,AdvSIMD}`; `hw.memsize` for RAM.
- **linux / android ARM** — AArch64 reports NEON unconditionally (Advanced SIMD is
  base ISA); 32-bit ARM probes `getauxval(AT_HWCAP)`. RAM from
  `sysconf(_SC_PHYS_PAGES) * _SC_PAGESIZE`.
- **win32 ARM** — `IsProcessorFeaturePresent(PF_ARM_NEON_INSTRUCTIONS_AVAILABLE)`;
  RAM from `GlobalMemoryStatusEx`.
- **web** — no runtime ISA probe is exposed; WASM SIMD128 maps to none of the x86/ARM
  ISA bits, so `features` and `ram_total` read `0`.
