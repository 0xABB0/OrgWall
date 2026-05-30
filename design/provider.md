# Melody Provider Module — Architecture Spec

Generic runtime-loaded plugin registry. Vendor SDKs, platform daemons, and any other out-of-tree code that the engine must call into are reached through a single, uniform shape: enumerate, request, release, with ABI versioning, callback bridging into a consumer-supplied completion pump, and bounded participation in consumer cache keys. The module is foundation — it has no sibling-module dependencies and stands beneath every consumer that needs to load third-party code (`gpu`, `sensor`, future consumers as they emerge).

This document is bound by the Ten Commandments of the Engine; tags are cited where a decision turns on one.

---

## 1. Inherited principles

**P1 — Emulate-to-equivalent.** A provider that cannot be supplied honestly returns `MissingProvider`. The registry never substitutes a silently-degraded alternative; the consumer asks for what it asked for and learns whether the host can deliver. Tier reporting on every grant lets a power user branch on "engine fallback vs vendor-native", but never on a lie (MEL-ENGINE-VIII).

**P2 — Full-control escape hatches.** Every convenience the registry offers — kind catalogs, preference orderings, callback marshalling, cache-key digesting — exposes the primitive beneath it. A consumer that wants to bypass the registry and dlopen / `LoadLibrary` a vendor SDK in-process is given the raw native handle path through U5-style interop (`provider_native(handle) -> { module, symbol_table }`); the registry is then a peer, not a gate. The simple path *is* the powerful path further along (MEL-ENGINE-II).

**MEL-ENGINE-III** governs registry costs. Provider loading is **demand-driven**: no provider is loaded, no DLL is mapped, no callback thread is parked until a consumer calls `request`. Enumeration probes presence without instantiating. The reactor pump is the consumer's, not the registry's — the module never spawns a background thread of its own (no thread shall be spawned in shadow).

**MEL-ENGINE-VIII** governs failure. Every fallible operation returns a per-action status with the encoding of §3.2 of gpu-rhi.md (`{ value, status }`, low-two-bit severity, per-action diagnostic enum). Provider load failure logs the underlying cause — `dlerror`, `GetLastError` formatted, vendor SDK init code, ABI mismatch — at the failure site through `mel_log_error("provider", ...)`. The enum stays compact; no detail is lost.

**MEL-ENGINE-X** governs scope. The module is the wingman. It does not appear in any user-visible API beyond the consumer that wraps it (`gpu_provider_request` and `tooling_provider_request` in `gpu/`, future `sensors_provider_request` in `sensor/`). A game never types `Mel_Provider_Registry` to be correct.

---

## 2. Public objects

### 2.1 `Mel_Provider`

A typed handle, value type, thin wrapper around `Mel_SlotMap_Handle`. One slotmap per registry instance. The generation field turns use-after-release into a loud `provider_alive(handle)` failure rather than a dangling-pointer dereference (MEL-ENGINE-VIII). Trivially copyable, threadable, serializable across the same process lifetime.

A `Mel_Provider` is opaque to consumers; the kind-specific surface (the function pointers the consumer actually calls) is reached through `provider_vtable(handle) -> const void*`, where the vtable layout is owned by the consumer module's kind catalog. The registry stores the vtable, the underlying native module handle, the ABI version triple, the SDK fingerprint, and the granted capability set; it does not interpret any of these.

### 2.2 `Mel_Provider_Registry`

One registry per consumer context. `gpu/` owns one registry per `Mel_Gpu_Device`; `sensor/` will own one per sensor host. Registries do not share state; a provider request against one does not implicitly satisfy a request against another, even when the underlying native module is the same DLL on disk — the registry tracks `request_count` per-(registry, module) so two consumers can each request the same SDK and only the second `release` unmaps it.

The registry holds:

- The kind catalog the consumer registered at construction (`provider_registry_create(desc, kinds[]) -> registry`). Each entry pins a kind ID, the expected ABI version range, the discovery search path, the load policy (`Runtime | StaticOnApple | StaticAlways`), and the vtable shape descriptor.
- A reactor reference. The registry calls back into this reactor when provider-owned completions fire; the consumer owns the pump. No registry-internal thread (MEL-ENGINE-III).
- The cache-key digester. A bounded blake3 (or equivalent) sink that consumers ask for when assembling pipeline / effect / asset cache keys; the registry contributes ABI version, SDK fingerprint, granted-cap mask, and architecture profile bytes through `provider_cache_key_contribute(handle, sink)`.

### 2.3 `Mel_Provider_Kind`

An opaque `u32` ID, defined by the consumer's catalog. The provider module never sees the semantics. `gpu/` defines `TemporalReconstruction`, `FrameGeneration`, `RayRegeneration`, `RadianceCache`, `LatencyControl`, `RayTracingOptimization`, `GpuProfiling`, `CrashDiagnostics`, `ShaderAnalysis`, `MobilePower`, `XrRuntimeBridge` and the `Tooling_*` kinds; `sensor/` will define its own; future consumers theirs. The catalog stays in the consumer's documentation, never here. (MEL-ENGINE-IX: parts compose. The kind catalog is a parameter, not a hard-coded enum baked into the registry.)

### 2.4 `Mel_Provider_Abi_Version`

A `{ major: u16, minor: u16, patch: u32 }` triple. Compatibility is asymmetric: a consumer requesting major M accepts any minor ≥ the minor it was built against; a major mismatch is a hard `AbiMismatch` status. The triple is part of every cache-key contribution; a minor bump that changes shader-visible behavior invalidates pipeline caches that named the old version. The SDK *vendor* version (DLSS 4.5, FSR 4.x, XeSS 3) is carried separately as a free-form fingerprint string and also contributes to keys.

---

## 3. Operations

The surface, given a registry `R`:

- `provider_enumerate(R) -> Mel_Provider_Desc[]` — probes the kind catalog without instantiating. Each descriptor reports kind, presence (`Found | NotInstalled | BlockedByPolicy | Incompatible`), discovered ABI version, SDK fingerprint, and the platform reason if blocked. Enumeration is `Concurrent` (caps are immutable for the registry's lifetime once kinds are pinned).
- `provider_request(R, kind, request_desc) -> { Mel_Provider, status }` — loads the native module if not yet mapped, calls the vendor init entry, validates ABI version against the kind catalog's accepted range, returns the handle with the granted capability mask on the status's warning bitset. Per-action status enum: `Ok | MissingProvider | AbiMismatch | InitFailed | BlockedByPolicy | LicenseAbsent | DeviceLost`. `request_desc` carries the consumer's reactor reference, the preference order over implementation variants (e.g. `[StreamlineDlss, NgxDirect]`), and any kind-specific bytes the consumer's catalog defines.
- `provider_release(handle)` — refcount-decrement on the underlying native module; when the last reference drops, the engine unmaps via `dlclose` / `FreeLibrary`, or leaves the static framework resident on Apple. Outstanding callbacks parked on the reactor pump are drained against the future-gated retire of §3.3 before the module is unmapped (MEL-ENGINE-VIII: no orphaned completions firing into freed memory).
- `provider_abi_version(handle) -> Mel_Provider_Abi_Version`
- `provider_sdk_fingerprint(handle) -> string` — vendor-version string, opaque, suitable only for cache-key digesting and human-readable telemetry.
- `provider_capabilities(handle) -> u64 bitmask` — the granted capability set, semantics owned by the consumer's kind catalog. The registry treats this as opaque bytes.
- `provider_call_class(handle, entry_index) -> Mel_Concurrency_Class` — reports each provider entry point's concurrency class (`Concurrent | SerializedPerObject | SerializedPerDevice`, same vocabulary as gpu-rhi.md §3.7 / U36). The engine's wrapper never widens a provider's narrower class; a `SerializedPerDevice` vendor call stays `SerializedPerDevice` and the consumer takes the lock.
- `provider_cache_key_contribute(handle, sink)` — pushes the ABI version triple, SDK fingerprint, granted-cap mask, and registered architecture-profile bytes into the consumer's cache-key sink. Consumers (gpu's pipeline cache, asset bundler, render-graph compiler) call this whenever a provider participates in producing a cached artifact; absence of this call means a provider change cannot invalidate a stale entry, so the consumer is contractually obliged to invoke it (MEL-ENGINE-VIII).
- `provider_native(handle) -> { module, symbol_table, init_descriptor }` — the P2 escape. Returns the raw dynamic-module handle (`void*` on POSIX, `HMODULE` on Win32, framework pointer on Apple), the resolved symbol table the registry built during `request`, and the descriptor passed to the vendor init. A consumer that wants to bypass every convenience and call the SDK directly may; the registry is then a peer of that path, not a gate. Releasing the native module without going through `provider_release` is undefined and asserted in debug.

Hot-path provider entry points — per-frame dispatch into a reconstruction provider, per-frame mark into a latency provider, counter-sample into a tooling provider — are **contract calls** in the registry's sense: the registry resolves the function pointer once at `request` time, stores it in the vtable, and consumers call through the vtable directly. No per-call registry lookup, no per-call status branch (MEL-ENGINE-III). The provider's own per-call fallibility is the provider's, surfaced through whatever per-action status the consumer's kind catalog defines.

---

## 4. ABI versioning model

The triple is `{ major, minor, patch }`. The consumer's kind catalog pins, per kind, an accepted `{ major, min_minor }` floor and an optional `max_major` ceiling. On `request`:

- If the discovered SDK reports a major outside the accepted range, the registry returns `AbiMismatch` without calling the SDK's init. A loud `mel_log_error` names the discovered version, the accepted range, and the kind.
- If the major matches and the minor is at or above the floor, the registry calls init. If init succeeds and reports a granted-cap mask that the consumer's catalog can interpret, the request grants.
- If init succeeds but the granted-cap mask is empty (the SDK loaded but cannot serve this device), the registry returns `MissingProvider` with the SDK fingerprint preserved for telemetry. The native module is unmapped immediately unless another active grant references it.

A patch-level change is opaque to the request decision but contributes to cache keys; a patch bump that affects shader output (a denoiser kernel revision, a frame-generator weight refresh) invalidates the corresponding pipeline / effect entries on next assembly. This is per-consumer policy — the registry only supplies the bytes; the consumer decides what to key on.

---

## 5. Callback bridging

Vendor SDKs signal completion through one of three mechanisms: a callback on a vendor-owned thread, a callback on the calling thread inside a blocking entry, or a vendor-managed event handle / fence the consumer polls. The registry normalizes all three onto the **consumer's reactor pump** supplied at `request` time:

- Vendor-thread callbacks are marshalled through a lock-free SPSC ring per provider; the registry's reactor adapter drains the ring on `pump_tick`. The vendor thread never crosses into engine code — it writes the completion record and returns.
- In-line callbacks on the calling thread complete synchronously and dispatch through the pump's same-thread fast path; no ring transit, no cross-thread cost (MEL-ENGINE-III).
- Event-handle / fence providers are wrapped behind a registry-managed waitable that the reactor polls (`WaitForSingleObjectEx`, `kevent`, `epoll_wait` on Linux, `dispatch_source` on Apple). The registry never spawns a wait thread; the reactor's existing wait infrastructure carries the load.

Completion records carry the originating `Mel_Provider` and a consumer-defined payload. The reactor delivers them to the consumer-supplied retire function; the registry never interprets the payload.

A provider whose init declared `requires_wait_thread = true` (a small number of legacy SDKs that demand one) gets an exception: the registry spawns exactly one named worker per such provider, scoped to the provider's lifetime, joined on `provider_release`. This is the only place the module ever creates a thread, and it is logged at info level so the cost is visible (MEL-ENGINE-III: every cost traceable).

---

## 6. Cache-key participation

Caches owned elsewhere — gpu's pipeline cache (gpu-rhi.md §6.5), the bundle cache (§6.4), the render-graph compiled-plan cache (§8.3), future asset and effect caches in `io-asset/`, `render-graph/`, `render-reconstruction/` — must invalidate when a provider's identity changes. The registry's `provider_cache_key_contribute(handle, sink)` is the one mechanism.

Bytes contributed, in order:

1. Kind ID (consumer-catalog defined).
2. ABI version triple.
3. SDK fingerprint string length + bytes.
4. Granted capability bitmask.
5. Architecture profile fingerprint (the consumer registered it at kind catalog construction; for `gpu/` it is the vendor + architecture-family + driver-branch triple from §9.1).
6. Optional kind-specific bytes the consumer's catalog declared, fetched through a registered `extra_key_bytes_fn`.

Order is fixed; the sink is opaque to the registry. Two providers with byte-identical contributions are cache-equivalent. A consumer that fails to call `contribute` for a provider participating in a cached artifact's production is producing stale caches; the contract is the consumer's, but the registry asserts in debug when an artifact-build path completes without contributing for at least one active provider that the consumer has marked as cache-relevant (`provider_mark_cache_relevant(handle)`).

---

## 7. Platform lowering

Discovery and loading reduce per platform. Lowerings are honest about platform differences; nothing is faked (MEL-ENGINE-VII: age forward, degrade gracefully).

**Windows.** `LoadLibraryExW` with `LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_USER_DIRS` against the consumer's registered search path, `GetProcAddress` for symbol resolution, `FreeLibrary` on release. Search path order is consumer-supplied; the registry never silently scans `%PATH%` (no stolen cycles, no surprise sources). Vendor SDKs that publish a registry-key install location (Streamline, Nsight Aftermath) are looked up through the consumer's registered probe function — the consumer queries the registry, the provider module just consumes the resolved path.

**Linux.** `dlopen(path, RTLD_LOCAL | RTLD_NOW)` to avoid lazy-binding latency on first call and to prevent symbol leakage into the engine's namespace, `dlsym` for resolution, `dlclose` on release. `RTLD_LAZY` is forbidden — lazy binding hides ABI breaks until the bad symbol is touched, defeating `MissingProvider` discipline (MEL-ENGINE-VIII).

**macOS / iOS.** Two paths. For SDKs that ship as standalone dylibs and are codesigning-permitted (a small set), `dlopen` works identically to Linux. For Apple's first-party frameworks that demand static framework linkage at app-bundle time (MetalFX is the canonical case; Metal Performance Shaders' ML paths similarly), the kind catalog declares `StaticOnApple` and the registry resolves symbols through the statically-linked framework at request time — no `dlopen`, no `dlclose`, the framework is resident for the process lifetime. `provider_release` on a `StaticOnApple` kind decrements the request count but never unmaps. This is platform-honest, not a kludge: Apple's signing contract makes runtime dlopen of first-party graphics frameworks unviable in shipping apps.

**Android.** `dlopen` against the consumer's search path, with the additional rule that vendor SDKs delivered through the system partition (Adreno tooling, Mali Streamline runtime) are reached only when the consumer's probe function reports them present; the registry never assumes `/system/lib64` contents.

**Web.** Provider loading is empty — there is no dynamic-library equivalent under the WebGPU origin model, and the wasm linker is not a runtime mechanism. `provider_enumerate` returns the static set the consumer registered as `StaticAlways` (the engine fallback variants of every kind); `provider_request` for any `Runtime` kind returns `MissingProvider`. This is honest, not a degraded shadow (MEL-ENGINE-VII).

The lowering is selected by the kind catalog's `load_policy` field, not by the registry guessing. A kind that names `Runtime` and runs on Web returns `MissingProvider` cleanly; a kind that names `StaticOnApple` on Linux is a catalog mistake and asserts at registry construction.

---

## 8. Concurrency

The registry is `Concurrent` on enumeration and `Concurrent` on `request` against distinct kinds; concurrent `request` calls naming the same kind serialize on the kind's internal init lock (the underlying SDKs almost universally forbid concurrent init). `provider_release` is `SerializedPerObject` on the handle. `provider_cache_key_contribute` is `Concurrent` against distinct sinks. The reactor pump tick is the consumer's responsibility and the consumer's class.

Provider entry points themselves declare their class per the wrapper rule of §2 above — the registry stores it, the consumer reads it through `provider_call_class`, and misuse asserts in debug through the consumer's thread-safety tracker (gpu-rhi.md U21 for `gpu/`; analogous infrastructure for other consumers).

---

## 9. Failure model

Every fallible registry operation returns the per-action `{ value, status }` shape of gpu-rhi.md §3.2. Status enums are per-action; the severity encoding (low-two-bit `Ok | Warned | Error`) is shared so generic code dispatches without committing to one vocabulary.

`Mel_Provider_Request_Status`: `Ok | MissingProvider | AbiMismatch | InitFailed | BlockedByPolicy | LicenseAbsent | DeviceLost`. Warning bitset reports `SubstitutedVariant` (the requested preference order's first entry was unavailable; a later entry was granted — the consumer must inspect `provider_capabilities` to learn which) and `DegradedAbi` (granted minor below the consumer's preferred minor but above the floor).

A provider failure cannot invalidate the registry, and the registry cannot invalidate its consumer's host object (the device, the sensor host, etc.). A device-lost surfaced *through* a provider is reported as `DeviceLost` and is the consumer's contract to handle, not the registry's.

---

## 10. Build-system facts, not registry facts

Licensing and redistribution constraints are build-system concerns, exactly as gpu-rhi.md §9.2 establishes for vendor SDKs. A build that does not ship a provider — because the build flag excluded the SDK, because licensing terms were not met, because the platform is web — reports the kind as `NotInstalled` at enumeration. The registry does not enforce license keys, does not phone home for entitlement checks, does not encrypt provider binaries. Those are `tools/build/` concerns, not runtime registry concerns (MEL-ENGINE-III: no cycle spent that the user did not ask for).

---

## 11. Sibling-module dependencies

None. The provider module is foundation. It depends on the engine's slotmap (for `Mel_SlotMap_Handle`), the engine's logger (for `mel_log_error`), and the engine's reactor pump protocol (for callback delivery), all of which sit at or below the provider module's layer.

Consumers that depend on `provider`:

- `gpu` (the present client) — every kind in gpu-rhi.md §9.2 through §9.8 registers through this module; the GPU-specific catalog of kinds lives in `gpu/`, not here.
- `sensor` (pinned, future) — vendor sensor SDKs (Tobii eye tracking, Ultraleap hand tracking, vendor IMU fusion libraries) will register through this module.
- `frame-latency` (pinned, future) — reaches NV Reflex / AMD Anti-Lag / Intel XeLL through the gpu-owned latency provider kinds, but the `frame-latency/` doc cross-references this module for the registry primitive.
- `render-reconstruction`, `media-video`, `xr` (pinned, future) — each will likely register kinds for the vendor reconstruction / codec / runtime-bridge SDKs in their domains.

The registry primitive does not change shape per consumer.

---

## 12. P2 escape hatch

A consumer that wants none of the above — that wants to dlopen a vendor SDK in-process, resolve symbols by hand, and call init without registry mediation — does so. `provider_native(handle)` is the bridge for the case where the consumer wants the registry's *discovery* (search-path resolution, ABI probe, platform-lowering pick) but the consumer's *invocation* (the consumer's own symbol table, the consumer's own callback marshalling). For the case where the consumer wants neither — the consumer dlopens directly — the registry is simply not used; `provider_request` is not invoked, no handle is allocated, and the SDK is the consumer's affair end-to-end. The registry is a convenience peer to that path, never a gate (MEL-ENGINE-II, MEL-ENGINE-IV).

The cost of the escape hatch: the consumer's caches no longer see the provider's identity through `provider_cache_key_contribute`, so the consumer is on its own for stale-cache invalidation. That is the price of full control, paid honestly.
