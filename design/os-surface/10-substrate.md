# Execution, memory & time substrate — OS-surface atlas (finer grain)
> domains D01–D10. Consumed by 00-atlas.md. Capability ceiling, axis-neutral; platform APIs are hints.

---

## Band I — Execution & runtime substrate

### D01 · process — process lifecycle & invocation
def: creating, running, observing, and reaping OS processes; this app's own lifecycle as the OS sees it.
- **spawn**:
  · fork + exec (POSIX, copy-on-write address space)
  · posix_spawn / vfork (combined spawn, file-action + attr specs)
  · CreateProcess / CreateProcessAsUser (win32, single call)
  · spawn under a different identity (runas / setuid-target / token)
  · suspended-create then resume (CREATE_SUSPENDED)
  · search-path resolution (execvp / PATH lookup) vs explicit image path
- **image & args**:
  · argv vector & quoting/escaping semantics (win32 single command line vs argv)
  · environment block (inherit / replace / merge / per-var set-unset)
  · working directory for the child
  · controlling-tty / detach-from-tty
- **exit & reap**:
  · exit codes & conventional ranges
  · wait / waitpid / waitid / WaitForSingleObject on child
  · async child-exit notification (SIGCHLD / kqueue EVFILT_PROC / pidfd / job-object completion port)
  · zombie reaping & reparenting-to-init / subreaper (PR_SET_CHILD_SUBREAPER)
  · pidfd / process handle as a waitable object
- **grouping**:
  · process groups & sessions (setsid / setpgid)
  · job objects (win32: limits, kill-on-close, nesting)
  · cgroup placement / membership query
  · session leader / foreground-pgrp (TTY job control)
- **stdio wiring**:
  · inherit / redirect / null the three standard handles
  · anonymous pipe to/from child (pipelines)
  · pseudo-terminal master/slave for child (boundary with D59)
  · explicit fd / handle inheritance set (posix_spawn file actions / STARTUPINFOEX inherit list)
  · close-on-exec / HANDLE_FLAG_INHERIT control
- **signals & structured exceptions**:
  · delivery & default dispositions; send (kill / raise / GenerateConsoleCtrlEvent)
  · handler installation (sigaction) & masking (sigprocmask)
  · real-time signals & queued signal values
  · synchronous fault signals (SIGSEGV/SIGFPE/SIGILL/SIGBUS) vs async
  · win32 structured exception handling (SEH / vectored handlers) as the analogue
  · signalfd / self-pipe / EVFILT_SIGNAL for synchronous reception
- **app lifecycle (own process as OS sees it)**:
  · foreground / background / suspend / resume transitions (mobile + win32 PLM)
  · app-state notifications (UIApplication / Activity / ProcessLifecycleOwner / WinRT EnteredBackground)
  · graceful-quit request & veto window
  · termination / kill on memory or policy (boundary with D09)
- **crash & abort**:
  · abort / fast-fail / __builtin_trap
  · core-dump generation & limits (rlimit core / minidump / .crash report)
  · crash-handler / unhandled-exception filter registration
  · OS crash reporter integration (ReportCrash / Watson / tombstoned / breakpad-style hooks)
- **identity & limits**:
  · pid / ppid / process-handle query
  · resource limits (setrlimit / getrlimit: cpu, fsize, nofile, stack, as)
  · own argv0 / image path / command-line query at runtime
- **priority**: nice / setpriority · win32 priority class · per-process QoS hint (boundary with D02)
↑beyond: launchd / systemd / SCM service models (declare, register, socket-activation, restart policy); setuid / capability drop (POSIX capabilities, setresuid, AppArmor/SELinux transition).
apps: shells, build systems, supervisors (tmux, foreman), launchers.
status: none.

### D02 · thread — threading, scheduling & affinity
def: OS-level threads of execution and how the scheduler treats them.
- **lifecycle**:
  · create / join / detach (pthread_create / std::thread / CreateThread)
  · cancellation / interruption request & points (pthread_cancel?)
  · thread handle / id query (self, gettid, GetThreadId)
  · exit & return-value retrieval
- **attributes at create**:
  · stack size & guard-page size
  · stack base / custom stack memory
  · detach-state & scheduling-inherit
- **TLS**: dynamic TLS keys (pthread_key / TlsAlloc) · `thread_local` / `__thread` static TLS · destructors / fini callbacks · TLS slot limits
- **name** — set/get debugger-visible thread name (pthread_setname_np / SetThreadDescription)
- **priority & QoS**:
  · POSIX priority + scheduling policy (SCHED_OTHER/FIFO/RR)
  · QoS classes (Darwin: user-interactive / user-initiated / utility / background)
  · win32 thread priority + relative levels
  · Android thread priority / cgroup-class (setpriority + SchedTune?)
  · priority inheritance / ceiling on locks (boundary with D03)
- **affinity & topology**:
  · CPU affinity mask set/get (sched_setaffinity / SetThreadAffinityMask / thread_policy_set affinity-tag)
  · core pinning to specific logical CPU
  · P-core / E-core topology query & class hint (QoS-driven on Darwin; explicit elsewhere)
  · NUMA-node thread placement (boundary with D08)
  · processor-group awareness (win32 >64 logical CPUs)
- **real-time scheduling**:
  · SCHED_FIFO / SCHED_RR fixed-priority
  · SCHED_DEADLINE (EDF: runtime/deadline/period)
  · win32 MMCSS pro-audio/-media scheduling category
  · Darwin time-constraint / workgroup join (os_workgroup, audio realtime)
  · RT-throttling & budget awareness
- **cooperative**: yield (sched_yield / SwitchToThread) · sleep / nanosleep / usleep (boundary with D10) · spin-wait / pause-hint (PAUSE / YIELD instruction) · pre-emption hint
- **introspection**: enumerate threads of own process · per-thread CPU time (boundary with D10) · stack-bounds query · suspend / resume another thread (SuspendThread; Darwin thread_suspend?)
↑beyond: SCHED_DEADLINE / MMCSS pro-audio priority; gang scheduling; os_workgroup parallel-work coordination; thread-QoS override / takedown.
apps: thread-pool runtimes, pro-audio engines, game engines.
status: none.

### D03 · sync — synchronization primitives
def: the OS-backed primitives threads use to coordinate.
- **mutex**:
  · plain / fast mutex
  · recursive
  · timed / try-with-timeout
  · error-checking
  · priority-inheritance / priority-ceiling protocol
  · robust mutex (owner-death recovery)
- **condition variable** — wait / signal / broadcast · timed wait · predicate re-check contract · CV bound to SRWLock (win32)
- **semaphore** — counting · binary · timed acquire · named cross-process (boundary below)
- **rwlock / shared** — read vs write acquire · timed · upgrade/downgrade? · writer-starvation policy · SRWLock (win32)
- **barrier** — fixed-count rendezvous · reusable / phased · pthread_barrier (deny on some libc?)
- **once-init** — pthread_once / std::call_once / InitOnceExecuteOnce · idempotent lazy init
- **direct wait-on-address**:
  · futex (linux: wait/wake, requeue, PI, robust list)
  · WaitOnAddress / WakeByAddress (win32)
  · os_sync_wait_on_address / __ulock (Darwin)
  · ParkingLot-style park/unpark primitive
- **atomics & memory model** — the hardware contract the OS guarantees: load/store/RMW · compare-exchange (weak/strong) · fetch-add/or/and · memory-ordering (relaxed/acquire/release/seq_cst) · fences · lock-free / wait-free guarantees per width · double-width CAS (CMPXCHG16B / LL-SC pairs)
- **named / cross-process**:
  · named mutex / semaphore / event (win32 kernel-named)
  · POSIX named semaphore (sem_open)
  · shared-memory-hosted futex / pthread mutex with PTHREAD_PROCESS_SHARED
  · cross-process eventing (boundary with D04 / D50)
- **policy**: spin-then-park (adaptive) · pure-spin · pure-park · backoff strategy awareness
apps: every concurrent runtime; lock-free libraries.
status: none.

### D04 · async-io — completion-based & readiness I/O multiplexing
def: the OS submission/completion engines that drive scalable I/O without a thread per fd.
- **readiness multiplexers**:
  · epoll (level/edge-triggered, EPOLLONESHOT, EPOLLEXCLUSIVE)
  · kqueue (EVFILT_READ/WRITE + non-IO filters)
  · poll / ppoll
  · select (legacy, fd_set limits)
  · /dev/poll / event ports (Solaris-class?)
- **completion engines**:
  · io_uring (SQ/CQ rings, linked ops, multishot)
  · IOCP (I/O completion ports, GetQueuedCompletionStatusEx)
  · GCD dispatch sources / dispatch_io (Darwin)
  · POSIX aio (aio_read/aio_write/lio_listio)
  · Windows ThreadPool I/O / overlapped + APC callbacks
  · RegisteredIO (RIO, win32 high-perf sockets)?
- **registration & lifetime** — add/modify/remove fd or handle · one-shot vs persistent · fd ownership & close-while-registered semantics · wakeup-fairness / starvation
- **scatter-gather** — readv / writev / preadv / pwritev · positional vector I/O · iovec / WSABUF batching
- **cancellation** — per-op cancel (io_uring_prep_cancel / CancelIoEx) · cancel-by-fd · cancel-all-on-close · cancellation-completion ordering
- **timeouts as events** — timer fused into the wait (epoll timeout, kevent timeout, io_uring timeout op, IOCP timeout) · per-op deadline · timerfd registered as a source (boundary with D10)
- **self-wake / cross-thread post** — eventfd · pipe self-pipe trick · EVFILT_USER · PostQueuedCompletionStatus · dispatch_async to the loop
- **io_uring fixed resources** — registered (fixed) buffers · registered (fixed) files · buffer rings / provided buffers · registered eventfd
- **back-pressure** — CQ overflow handling · SQ full / submission throttling · watermark / readiness re-arm policy
↓under: io_uring SQPOLL kernel-side submission polling (kernel thread drains the SQ); io_uring IOPOLL for O_DIRECT NVMe.
apps: async runtimes, high-fanout servers, databases.
status: none.

### D05 · dylib — dynamic linking & runtime code loading
def: bringing executable code into the address space after launch.
- **load / unload**:
  · dlopen / dlclose (POSIX)
  · LoadLibrary(Ex) / FreeLibrary (win32, flags: DONT_RESOLVE, ALTERED_SEARCH_PATH, datafile)
  · NSBundle / dyld load of macOS bundles & frameworks
  · ref-counting & idempotent re-open
- **search & resolution** — RPATH / RUNPATH / @rpath / @loader_path / @executable_path · LD_LIBRARY_PATH / DYLD_*_PATH awareness · win32 DLL search order & SetDllDirectory / AddDllDirectory · default-loaded handle (RTLD_DEFAULT / GetModuleHandle(NULL))
- **symbol lookup** — by name (dlsym / GetProcAddress) · by ordinal (win32) · versioned symbols (glibc symbol versioning / .symver) · weak symbols & weak-import · re-export / forwarder symbols
- **binding & scope**:
  · lazy vs now binding (RTLD_LAZY / RTLD_NOW / DYLD_BIND_AT_LAUNCH)
  · local vs global scope (RTLD_LOCAL / RTLD_GLOBAL)
  · deepbind / first-wins resolution order
  · interposition / symbol preloading (LD_PRELOAD / DYLD_INSERT_LIBRARIES)
- **introspection**:
  · dladdr (addr → symbol/module)
  · dl_iterate_phdr / loaded-module enumeration (EnumProcessModules / _dyld_image_count)
  · load address & ASLR slide query (_dyld_get_image_vmaddr_slide)
  · backtrace + symbolication (backtrace_symbols / CaptureStackBackTrace + SymFromAddr)
- **init / fini** — `__attribute__((constructor/destructor))` · DllMain (PROCESS/THREAD attach/detach) · TLS callbacks · init-array / fini-array ordering · static-initializer ordering across libs
- **TLS in shared objects** — dynamic TLS model (general/local-dynamic) · TLS in dlopen'd libs · per-module TLS limits
- **packaging of loadables** — .so / .dylib / .dll / .framework / macOS loadable bundle (.bundle) · Android packaged .so in APK (extractNativeLibs / uncompressed-aligned) · fat / universal binary slice selection · wasm: no dlopen (dynamic-linking proposal / JS module import as the degenerate axis?)
↑beyond: macOS code-signing constraints on loadable bundles; hardened-runtime library validation (com.apple.security.cs.disable-library-validation); win32 DLL Authenticode / forced-integrity; Android namespace-isolated linker (classloader-namespace, greylist).
apps: plugin hosts (DAWs, editors), scripting embeds.
status: none.

### D06 · jit — runtime code generation & W^X execution
def: writing machine code at runtime and making the OS let you run it.
- **executable page mapping** — mmap PROT_EXEC · VirtualAlloc PAGE_EXECUTE_READWRITE/READ · mprotect / VirtualProtect transitions · vm_protect (Darwin) · WebAssembly.Module compile as the wasm analogue
- **W^X toggling**:
  · MAP_JIT region + pthread_jit_write_protect_np (Apple silicon)
  · per-thread RW↔RX flip (toggle, not per-page)
  · dual-mapping (RW alias + RX alias of same physical page)
  · separate write/exec handles (mremap / memfd dual map)
- **icache coherency** — __builtin___clear_cache / sys_icache_invalidate · FlushInstructionCache (win32) · DSB/ISB barrier requirements on ARM
- **signing & entitlement gating**:
  · com.apple.security.cs.allow-jit / allow-unsigned-executable-memory (macOS)
  · iOS JIT deny (no entitlement for App Store; debugger-attach exception)
  · win32 ACG / arbitrary-code-guard process mitigation awareness
  · Android: executable anon mmap allowed (W^X enforced API 29+?)
  · wasm: engine-managed, no raw page control
- **unwind & guard** — register generated-frame unwind info (__register_frame / RtlAddFunctionTable / .eh_frame) · guard pages around code arenas · stack-unwind support for JIT frames in profilers
- **profiling-symbol registration** — perf map (/tmp/perf-PID.map) · jitdump (perf inject) · VTune JIT profiling API · ETW JIT events (win32) · GDB JIT interface (__jit_debug_register_code)
↓under: hardware W^X enforcement (Apple silicon APRR / permission-overlay registers); BTI / CET landing pads & indirect-branch-target enforcement on generated code; PAC signing of generated pointers.
apps: language VMs, regex engines, shader/codegen pipelines.
status: spawn (`design/jit-*.md`).

### D07 · cpu — CPU introspection & performance counters
def: discovering what the CPU is and measuring what it does.
- **ISA feature detection**:
  · x86: SSE/SSE2/…/AVX/AVX2/AVX-512 (per-subset) / AMX, crypto (AES-NI/SHA), BMI, FMA — via CPUID
  · ARM: NEON/ASIMD, SVE/SVE2 (+ vector length), crypto ext, dotprod, BF16, FEAT_* — via HWCAP / sysctl / commpage
  · query path: CPUID · getauxval(AT_HWCAP/HWCAP2) · sysctlbyname(hw.optional.*) · IsProcessorFeaturePresent
  · OS-enabled vs CPU-present (AVX needs XCR0/XSAVE OS opt-in; SVE kernel support)
- **identity** — vendor / brand string · family/model/stepping · microarchitecture id · core type (perf/efficiency) tagging
- **topology**:
  · logical CPU count (online vs configured)
  · physical core count & sockets/packages
  · NUMA node count & CPU↔node map (boundary with D08)
  · SMT / hyperthread sibling map
  · processor groups (win32)
  · CPU-set / affinity-allowed mask (cgroup-restricted view)
- **cache topology** — per-level (L1i/L1d/L2/L3) size · line size · associativity · sharing map (which cores share which cache) · CLFLUSH / cache-line-size for alignment
- **cycle / time counters** — rdtsc / rdtscp (+ invariant-TSC flag) · cntvct_el0 + cntfrq (ARM) · QueryPerformanceCounter mapping · serialization (rdtscp / lfence) requirements (boundary with D10)
- **PMU / hardware perf counters**:
  · perf_event_open (linux: HW/SW events, sampling, groups, ring buffer)
  · ETW / PerfInfo + CPU counters (win32)
  · kperf / kpc (Darwin private?)
  · PAPI-style portable abstraction (lib, not OS)
  · per-thread vs per-core vs system-wide counting
- **frequency & scaling** — current / base / max frequency · governor / power-policy state (cpufreq, boundary with D67) · per-core frequency where exposed
↓under: raw MSR read/write (/dev/cpu/*/msr); direct PMU register programming; perf_event mmap'd kernel ring buffers; Intel PT / ARM CoreSight trace.
apps: profilers, dispatch-by-ISA libraries, schedulers.
status: none.

---

## Band II — Memory & address space

### D08 · vmem — virtual memory & address space
def: how the app maps, protects, and shares pages with the OS.
- **reserve / commit**:
  · mmap MAP_ANON (reserve+commit in one) vs reserve-then-touch
  · VirtualAlloc MEM_RESERVE / MEM_COMMIT split (win32 two-phase)
  · mach vm_allocate / vm_map (Darwin)
  · decommit / release (munmap / VirtualFree MEM_DECOMMIT/RELEASE)
  · placed / fixed-address mapping (MAP_FIXED / MAP_FIXED_NOREPLACE)
- **protection** — per-page RWX (mprotect / VirtualProtect) · guard pages (PAGE_GUARD / PROT_NONE sentinels) · no-access reservation · write-watch (win32 MEM_WRITE_WATCH)
- **memory-mapped files** — file-backed (read/write/copy-on-write/MAP_PRIVATE vs SHARED) · anonymous shared (MAP_SHARED|MAP_ANON) · partial / offset mapping window · msync / FlushViewOfFile flush control · mapping growth / remap (mremap)
- **shared memory**:
  · POSIX shm_open + ftruncate + mmap
  · SysV shmget / shmat
  · win32 CreateFileMapping / MapViewOfFile (named & anonymous)
  · memfd_create (+ seals: shrink/grow/write)
  · Android ashmem / ASharedMemory
  · cross-process handle/fd transfer of the mapping (boundary with D50)
- **large / huge pages** — explicit hugetlbfs / MAP_HUGETLB (2M/1G) · transparent huge pages (madvise MADV_HUGEPAGE) · win32 large-page (SeLockMemory privilege) · superpage hints
- **wiring / pinning** — mlock / mlockall · VirtualLock · vm_wire · working-set min/max (SetProcessWorkingSetSize) · unevictable / pinned for DMA awareness
- **advise / lifecycle** — madvise (WILLNEED / DONTNEED / FREE / COLD / PAGEOUT / SEQUENTIAL / RANDOM) · MADV_DONTFORK / WIPEONFORK · DiscardVirtualMemory / OfferVirtualMemory (win32) · purgeable hint (boundary with D09)
- **NUMA placement** — bind / preferred / interleave policy (mbind / set_mempolicy) · per-node allocation (VirtualAllocExNuma / numa_alloc_onnode) · migrate_pages · node query for an address
- **layout & query** — address-space map walk (/proc/self/maps · vm_region · VirtualQuery · mach vm_region_recurse) · ASLR slide / randomization state · stack/heap/guard bounds · page size & allocation granularity query
- **copy-on-write** — fork COW semantics · MADV_DONTFORK exclusion · explicit COW mapping (MAP_PRIVATE file) · write-fault behavior
- **userfaultfd** — register ranges for fault handling · missing-page & write-protect modes · resolve via copy/zeropage/continue · second-process fault servicing
↑beyond: userfaultfd live migration / on-demand paging; MTE (memory tagging) / pointer tagging / TBI; sub-page protection (Intel SPP?); CHERI capabilities?
apps: allocators, databases, GC runtimes, zero-copy IPC.
status: none.

### D09 · mempolicy — pressure, budgets & OOM
def: the OS telling the app to give memory back, and killing it if it won't.
- **pressure notifications**:
  · DISPATCH_SOURCE_MEMORYPRESSURE / os_memorypressure (Darwin: warn/critical)
  · ComponentCallbacks.onTrimMemory / onLowMemory (Android levels)
  · CreateMemoryResourceNotification / QueryMemoryResourceNotification (win32)
  · cgroup v2 memory.pressure (PSI) / memory.events
  · UIApplication didReceiveMemoryWarning (iOS)
- **footprint query** — resident / dirty / swapped (task_info phys_footprint · /proc/self/smaps_rollup · GetProcessMemoryInfo PROCESS_MEMORY_COUNTERS) · per-region attribution · peak working set · committed / private bytes
- **budget & kill behavior**:
  · jetsam priority bands & per-app memory limit (iOS/macOS)
  · Android lowmemorykiller / LMKD oom_adj / oom_score
  · linux OOM-killer score (oom_score_adj) & cgroup memory.max / memory.high
  · win32 job-object memory limit + commit limit
  · query own limit / distance-to-limit where exposed
- **purgeable / reclaimable** — NSPurgeableData / NSCache eviction · ashmem ASHMEM_PIN/UNPIN purge · MADV_FREE volatile ranges (boundary with D08) · OfferVirtualMemory reclaimable (win32) · begin/end-content-access semantics
- **low-memory mode** — battery/low-power-driven memory restriction awareness · app-standby / cached-app state · swap / compression state query (memory_pressure compressor)
apps: caches, image/texture pools, anything that must survive backgrounding.
status: none.

---

## Band III — Time

### D10 · time — clocks, timers & calendars
def: every notion of "when" the OS provides.
- **wall / realtime** — CLOCK_REALTIME / gettimeofday · GetSystemTimePreciseAsFileTime · NSDate / mach_continuous epoch mapping · settable & subject to NTP step
- **monotonic family**:
  · monotonic (CLOCK_MONOTONIC / mach_absolute_time / QueryPerformanceCounter) — not affected by step
  · raw monotonic (CLOCK_MONOTONIC_RAW — no NTP slew)
  · boottime / uptime including-suspend (CLOCK_BOOTTIME / mach_continuous_time / GetTickCount64)
  · uptime excluding-suspend (CLOCK_UPTIME_RAW)
  · resolution / period query (clock_getres / mach_timebase_info / QueryPerformanceFrequency)
- **cpu / thread time** — process CPU (CLOCK_PROCESS_CPUTIME_ID / GetProcessTimes) · per-thread CPU (CLOCK_THREAD_CPUTIME_ID / thread_info / GetThreadTimes) · getrusage user/sys split
- **timers**:
  · one-shot & periodic (timer_create / timerfd / SetWaitableTimer / dispatch_after-source / kqueue EVFILT_TIMER / CreateTimerQueueTimer)
  · interval / TimerFD periodicity & overrun count
  · absolute-deadline vs relative
  · high-resolution timer (multimedia timer / timeBeginPeriod / hrtimer)
  · as-an-event integration (registered into D04 multiplexer)
- **coalescing & leeway** — timer leeway / slack (dispatch leeway · SetWaitableTimerEx tolerable-delay · prctl PR_SET_TIMERSLACK) · coalescing for power (boundary with D67) · high-resolution opt-out of coalescing
- **sleep / delay** — nanosleep / clock_nanosleep (abs/rel) · usleep · Sleep / SleepEx (alertable) · WaitForSingleObjectEx with timeout · busy-wait vs yield (boundary with D02)
- **timezone & DST** — IANA tzdata lookup · current zone & offset · DST transition rules · per-zone history · zone-change notification (boundary with D69)
- **calendar arithmetic** — Gregorian + non-Gregorian calendars · date components / decomposition · interval & duration math · leap-second handling / smearing awareness (boundary with D61 for formatting)
- **sync state** — NTP-synced / time-source quality flag (adjtimex / ntp_gettime status · GetSystemTimeAdjustment) · clock-step / discontinuity notification · slew vs step in progress
- **clock-domain correlation** — host ↔ device ↔ media time-base mapping (CMClockGetHostTimeClock / mach_continuous vs audio/video sample clock) · timestamp rebasing across domains · the camera A/V + IMU correlation case
↑beyond: hardware timestamping (PTP / PHC / SO_TIMESTAMPING); audio/video sample-clock disciplining; PTP IEEE-1588 clock domains.
apps: schedulers, media sync, games (frame pacing), finance.
status: none.
