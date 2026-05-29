# Melody Build System — Clean-Room Rewrite Plan

A clean-room reimplementation of the build system, **keeping the paradigm and replacing the
implementation**. The paradigm — `build.c` is an ordinary C program that constructs a target
graph through a small API, no bespoke DSL — is already the right one (it is precisely Zig's
`build.zig` model). What this rewrite buys is the three
things the current implementation lacks: a *small composable* API instead of a 64-verb
combinatorial surface, a *content-addressed cache* instead of leaning on Ninja, and *modules
and host tools as first-class targets* — the last being the Phase-0 prerequisite the
reflect-meta substrate (`docs/reflect-meta.md` §4, R1–R3) demands.

> **Naming caveat.** Identifiers here are provisional, chosen to carry the design.
>
> **Relation to reflect-meta.** This document discharges reflect-meta's R1 (modules-as-targets),
> R2 (host-target), and R3 (the `public/private/src/meta` directory convention). Those three are
> stated there as requirements; here they are designed.

---

## 1. Premise — same paradigm, new implementation

The decision to clean-room rather than refactor-in-place was made deliberately: the current
~3.2K lines across `tools/build/runner_*.c` are *soundly factored* but carry two structural
commitments worth shedding at the root rather than retrofitting — the global per-invocation
variant (which cannot express "build this dependency for the host while building its consumer
for the target") and the Ninja-as-incrementality model (which competes with the content cache
`prd/10` actually wants). Retrofitting host-target and a real cache onto those two commitments
is more total work, and more fragile, than reconstructing on a model that has both designed in.

What "be more like Zig" resolves to, concretely, is exactly three things — none of them a
paradigm change:

- **A small composable API.** Zig's virtue is a tiny surface (`addExecutable`, `addStaticLibrary`,
  `b.dependency`, a handful of step builders) composed freely. The current 64-entry surface is the
  opposite: every property crossed with `{base, _on, _on_gpu, _on_runtime}` and `{public, private}`.
  The underlying *data* model (`Prop{value, mask, runtime, gpu_backend}`) is already right; it is
  merely surfaced as N functions instead of one selector. §5 collapses it.
- **Content-addressed caching.** Zig and Bazel both key artifacts by a hash of their inputs. `prd/10`
  specifies exactly this; it is simply unbuilt. §7 builds it.
- **First-class cross-axis targets.** Zig's `b.resolveTargetQuery` makes "this artifact for *that*
  target" ordinary. The current global variant makes it impossible. §4 generalizes it, which is
  what makes host-target (R2) fall out for free.

Switching to CMake/Gradle/Bazel/Zig-the-tool was rejected: each re-acquires this paradigm and adds
a DSL or runtime the project deliberately refused.

---

## 2. What we keep, what we discard

**Keep (port forward, possibly verbatim):** the stage model (configure → compile → link → package)
with layered framework-default-then-user callbacks; the CMake-style PUBLIC/PRIVATE property
propagation; platform-chain source gating by directory and basename (`macos→apple→posix`, the
`*.win32.c`/`.posix.c`/`.m` suffix rules); the third-party helpers (`mel_tp_*`); the Android Gradle,
Apple plist/bundle, and Win32 `.rc` configure/package machinery; the `platform[:backend[:runtime]]`
CLI grammar and the `--release`/`--debug`/`--gpu` flags; the synthesized `melody-test` target.

**Discard (reconstruct or delete):** the global `g_backend`/`g_gpu_backend`/`g_runtime` variant
(replaced by a per-target resolved `Variant`, §4); the 40-odd property-verb variants (replaced by
the selector, §5); Ninja generation (`runner_ninja.c`, replaced by the cache + a direct parallel
dispatcher, §7 — *decision to ratify*); the umbrella-globs-all-modules model in `modules/build.c`
(replaced by modules-as-targets, §3); the `runner_codegen.c` bolt-on island (replaced by a
first-class compile-substage participant, §6).

---

## 3. The target model

`prd/10`'s thesis taken literally: **everything is a target**, down to module granularity. A target
is declared by a `build.c` exporting `bool project(Mel_Build_Target *t)`, discovered and `dlsym`'d as
today. Kinds widen:

```c
typedef enum {
    MEL_TARGET_LIBRARY,
    MEL_TARGET_APPLICATION,
    MEL_TARGET_THIRD_PARTY,
    MEL_TARGET_META,
    MEL_TARGET_MODULE,     // new: an engine module, separately compiled, with declared availability
    MEL_TARGET_HOST_TOOL,  // new: always built for the host axis regardless of the build's target
} Mel_Target_Kind;
```

**Modules become targets (R1).** Each `modules/<m>/` gains a `build.c` declaring kind `MODULE`, its
availability, and its dependency edges. Availability is a property *of the module*, declared once and
inherited by every consumer — retiring the backwards model where `modules/build.c` excludes `server`
on web and `gui`/`gpu` under wasi from the consumer side. This matches `prd/04`'s already-locked rule
verbatim: *depending on a module from a target compiled for a platform it does not support is a build
error, requiring no `#if` in consumer code.* The dependency resolver (`topo_visit`, which already
prunes deps by platform support) is extended to *error* rather than silently prune when an
explicitly-requested module is unavailable, and to prune only transitive deps reachable solely
through an unavailable optional one.

**Boilerplate is bounded by defaults, not by ceremony.** A module with no constraints writes a
near-empty `build.c` (`name`, `kind = MODULE`; sources auto-discovered from `src/`). Only a module that
constrains availability or declares non-trivial deps writes more. This honors the zero-config ethos
the auto-discovery established (MEL-ENGINE-V) while still making every module a real target.

The dependency DAG carries the reflect-meta modules (`reflect` ← `clang`, `codegen`; `compiler` ← `jit`)
and their availability matrix directly (reflect-meta §3), so "jit on web is a build error" needs no
special case — it is the generic R1 rule.

---

## 4. The axis & variant model

The current five axes (platform, config, backend, gpu, runtime) are correct; the defect is that they
live in *globals bound once per invocation*, so the whole graph builds for one variant. Host-target
(R2) breaks that: within one `./nob build app`, the reflection driver's deps (`clang`, `codegen`,
`reflect`) build for the **host** while the app builds for the **target**. So the variant moves onto
the resolution context, and a target is resolved against a *requested* variant rather than a global:

```c
typedef struct {
    Mel_Platform platform;
    Mel_Config   config;
    const char  *backend;
    const char  *gpu;
    const char  *runtime;
    bool         host;      // resolve for the host toolchain/arch, ignoring the target axes
} Mel_Variant;
```

Dependency edges may *rebind* the axis. A normal edge inherits the consumer's variant; a host edge
forces `host = true` on the subgraph it roots:

```c
mel_depends(t, "reflect");            // inherits the consumer's variant
mel_depends_host(t, "codegen");       // this subgraph resolves for the host
```

`MEL_TARGET_HOST_TOOL` targets are implicitly host-rebound. The build orchestrator therefore resolves
the graph as a set of (target, variant) pairs rather than (target) under one global variant — a target
may legitimately appear twice, once host once target (e.g. `reflect`, built host for the driver and
target for `RUNTIME`-retained shipping). The content cache (§7) keys on the full variant, so the two
builds never collide.

This same generalization subsumes Android's existing per-ABI sub-axis (`Cross`), which today is a
side-channel descriptor; it becomes another variant field, removing a special case.

---

## 5. The composable selector — the central ergonomic win

Replace the 40-odd `add_*`/`_on`/`_on_gpu`/`_on_runtime` × `{public,private}` functions with **four
property verbs**, each taking one composable selector. The selector is a designated-initializer
compound literal; an unset field means "any", so a selector reads as exactly the axes it constrains:

```c
typedef struct {
    uint32_t     platforms;    // bitmask; 0 = any. MEL_ON(MACOS)|MEL_ON(IOS) for a set.
    const char  *backend;      // NULL = any
    const char  *gpu;          // NULL = any
    const char  *runtime;      // NULL = any
    Mel_Config   config;
    bool         has_config;   // distinguishes "config = DEBUG" from "unset"
} Mel_When;

#define MEL_ON(p)   (1u << (MEL_PLATFORM_##p))
#define WHEN(...)   ((Mel_When){ __VA_ARGS__ })
#define ALWAYS      ((Mel_When){ 0 })

MEL_API void mel_cflags_(Mel_Build_Target *t, Mel_Visibility vis, Mel_When when, ...);
MEL_API void mel_defines_(Mel_Build_Target *t, Mel_Visibility vis, Mel_When when, ...);
MEL_API void mel_includes_(Mel_Build_Target *t, Mel_Visibility vis, Mel_When when, ...);
MEL_API void mel_link_(Mel_Build_Target *t, Mel_Visibility vis, Mel_When when, ...);
#define mel_cflags(t, vis, when, ...)   mel_cflags_(t, vis, when, __VA_ARGS__, NULL)
#define mel_defines(t, vis, when, ...)  mel_defines_(t, vis, when, __VA_ARGS__, NULL)
#define mel_includes(t, vis, when, ...) mel_includes_(t, vis, when, __VA_ARGS__, NULL)
#define mel_link(t, vis, when, ...)     mel_link_(t, vis, when, __VA_ARGS__, NULL)
```

Before, the `melody` target's Vulkan and Cocoa flags read as four distinct verbs:

```c
mel_build_add_link_flag_on_gpu(t, MEL_PUBLIC, "vulkan", "-lvulkan");
mel_build_add_link_flag_on(t, MEL_PUBLIC, MEL_PLATFORM_MACOS, "-framework", "Cocoa");
mel_build_add_cflag_on(t, MEL_PUBLIC, MEL_PLATFORM_MACOS, "-I/opt/homebrew/include");
mel_build_add_link_flag_on_runtime(t, MEL_PUBLIC, "emscripten", "--use-port=emdawnwebgpu");
```

After, one verb each, the gate read off the selector — and a *set* of platforms is now expressible
without repetition (the `Prop` mask already supported it; the old API could not express it):

```c
mel_link(t, MEL_PUBLIC, WHEN(.gpu = "vulkan"), "-lvulkan");
mel_link(t, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "Cocoa");
mel_cflags(t, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-I/opt/homebrew/include");
mel_link(t, MEL_PUBLIC, WHEN(.runtime = "emscripten"), "--use-port=emdawnwebgpu");
```

The four verbs × the one selector replace the entire `_on`/`_on_gpu`/`_on_runtime` family. Module and
source exclusion fold into the same model: `mel_exclude_source(t, WHEN(.platforms = …), "surface.m")`
replaces `mel_build_exclude_source_on`, and module availability is the module's *own* `set_platforms`
plus a `WHEN`-gated unavailability rather than the consumer's `exclude_module_on`.

This is the change that makes the surface *feel* like Zig: a small set of composable verbs over one
predicate, rather than a verb per axis-combination.

---

## 6. The stage model

Keep the four stages and the layered-callback execution (`run_stage`: framework default first, then
user callbacks in registration order, `suppress_default` to opt out). Two refinements:

- **Compile gains explicit sub-stages** as `prd/10` already names: `fetch_sources` (gather the TU
  list, the hook for generated sources) and `compile_source` (run the compiler). The reflect/codegen
  pipeline (reflect-meta §9) registers into `fetch_sources` as a **first-class participant**, not a
  bolt-on: it runs the host-built driver (a `HOST_TOOL` subgraph, §4) and appends `reflect.generated.c`
  to the TU list. The 132-line `runner_codegen.c` island dissolves into an ordinary compile-substage
  callback over the cache.
- **Defaults stay data-driven** (`k_defaults[stage]`), gaining a compile default that performs the
  cache-aware build of §7.

---

## 7. Content-addressed cache (fulfilling prd/10)

The missing piece, and the second "be more like Zig/Bazel" win. Every compiled object, linked
artifact, and packaged result is keyed by a hash over its complete inputs:

- the source file bytes;
- the bytes of every header the compiler reports via `-MD` (the dependency-file scan);
- the compiler binary's path and version string;
- the full resolved flag set (cflags, defines, includes) — *post-selector*, so a flag that applies
  only under one variant changes only that variant's key;
- the full `Mel_Variant` (so host and target builds of the same TU are distinct entries);
- the hashes of every transitive dependency artifact.

Objects live at `build/cache/<hash>/<name>.o`; per-variant build dirs under
`build/<target>/<platform>/<config>/` are populated by hard-linking from the cache. Switching
debug↔release, or host↔target, becomes a cache lookup, not a recompile, when inputs are unchanged.
`./nob cache gc` evicts entries older than a threshold not referenced by any current build.

**Decision to ratify — Ninja's fate.** With a content cache as the source of truth for "is this object
current?", Ninja's incrementality is redundant, and two competing incrementality models is a smell.
Recommendation: **drop Ninja**, and dispatch cache-misses through a bounded parallel job runner built
on `nob`'s existing async `Cmd` infrastructure (`prd/10` already assumes compile parallelism comes from
`nob`). This deletes `runner_ninja.c` and removes a special-cased emitter. The cost is reimplementing a
small parallel scheduler; the benefit is one incrementality model and no generated `build.ninja` to
reason about. The alternative — keep Ninja as the executor *under* the cache — is less work now but
preserves the dual model. (Android/Gradle and Apple/Xcode-less bundling are unaffected either way; they
are package-stage concerns.)

---

## 8. Directory convention (R3)

Modules move from `include/` + `src/` to **`public/`** (API on the include path for dependents),
**`private/`** (internal headers, not exported), **`src/`** (target-axis implementation, platform-chain
gated as today), and **`meta/`** (axis-agnostic metaprogram code — host-compiled it is the reflection
driver, target-compiled it ships into an app's embedded compiler; reflect-meta §4/§7). The folder *is*
the host/target and program/metaprogram signal — no flag needed, exactly as `src/<platform>/` gating is
implicit today. Discovery (`runner_discovery.c`) learns the four roots; `add_modules`'s "`<m>/include`
becomes a public include, `<m>/src` a source root" generalizes to the four-way split.

This is a repo-wide refactor touching every module; it is mechanical (rename `include/`→`public/`, leave
`src/`) and lands as its own migration step (§12), decoupled from the framework rewrite so the two are
not entangled.

---

## 9. Discovery, registry, dependency resolution

Unchanged in spirit: walk `apps/*/build.c`, `modules/*/build.c`, third-party `build.c`; compile each to
a DLL; `dlsym` `project()`; register. The registry and `topo_visit` extend per §3 (module targets,
availability-as-error) and §4 ((target, variant) pairs, host rebinding). The synthesized `melody-test`
target survives as-is, now depending on module targets rather than globbing `modules/*/test`.

---

## 10. CLI & entry

The `mel_build_main` surface is kept verbatim: `build|run|debug|test|configure|compile|link|package`,
the `platform[:backend[:runtime]]` positional with empty-field axis fallback, `--release`/`--debug`,
`--gpu`, `-- <test args>`, root-only invocation. New: `cache gc`. The host-target work is invisible at
the CLI — `./nob build <app>` transparently builds the host driver subgraph before the app's compile
stage.

---

## 11. Code layout of the new framework

Framework code stays under `tools/build/` (the rewritten translation units), archived into
`libmelbuild.a` and linked by every `build.c`, with the runner compiled into the `nob` driver as today
(`nob.c` includes the build TU then the runner TU). Scaffolding *templates* move to `lib/build/<platform>/`
per `prd/10` (Gradle, xcconfig/plist, `.rc`/manifest, web bundle config), separating the framework's code
from its data. Proposed TUs, by concern, mirroring the current clean split but smaller: `target` (model,
registry), `variant` (axis resolution, host rebinding), `select` (the `Mel_When` engine), `graph` (topo +
(target,variant) orchestration), `cache` (hashing, eviction, hard-linking), `compile` (substages, parallel
dispatch), `link`, `discover`, `platform`, and the package backends (`apple`, `android`, `win32`, `web`).

---

## 12. Migration plan

Clean-room demands a parallel path so the repo never bricks:

1. **Build the new framework beside the old.** New TUs under `tools/build/`, gated behind a `nob.c`
   switch, until parity. The old runner keeps the repo building throughout.
2. **Port the consumers.** There are few `build.c` files (the `melody` umbrella, the apps, the
   third-party targets) — rewrite them onto the selector API (§5) and split the umbrella into per-module
   `build.c` (§3). Mechanical and reviewable.
3. **Directory convention (§8)** as a discrete step: rename module roots to `public/private/src/meta`,
   update discovery. Independently revertible.
4. **Parity gate.** For each existing target/variant, assert the new system produces a byte-identical (or
   provably equivalent) artifact to the old, and that a no-change rebuild is a cache no-op and a
   config-switch is a cache hit. This is the correctness oracle, mirroring reflect-meta's byte-identical
   `to_string` gate.
5. **Flip and delete.** Switch `nob.c` to the new runner; delete the old `runner_*.c` and `runner_ninja.c`.

Only after the framework is live does reflect-meta Phase 1 build on it (host-target driver, modules
`reflect`/`clang`/`codegen`).

---

## 13. Phasing

1. **Target & variant core.** New target model (§3 kinds), per-target `Variant` resolution with host
   rebinding (§4), registry/topo extensions. No cache yet; compile via a trivial always-rebuild dispatcher
   to reach end-to-end first.
2. **Selector API.** The `Mel_When` engine and the four property verbs (§5); port the `melody` umbrella
   and apps onto it; delete the old verb family.
3. **Modules-as-targets.** Split `modules/build.c` into per-module `build.c` with declared availability +
   deps (§3); availability-as-error in the resolver; port the test target.
4. **Content cache.** The hashing/eviction/hard-link cache (§7); ratify and execute the Ninja decision;
   parity gate on incrementality.
5. **Directory convention.** `public/private/src/meta` (§8), repo-wide.
6. **Flip & delete** (§12.5). The build system is now the foundation reflect-meta Phase 1 stands on.

Phases 1–4 are sequenced by dependency; 5 can land any time after 3; 6 is the cutover.

---

## 14. Decisions to ratify

- **Ninja (§7).** Drop it for an owned cache + parallel dispatcher (recommended), or keep it as the
  executor beneath the cache? This is the largest single fork — it decides whether `runner_ninja.c` dies.
- **Module `build.c` floor (§3).** Is a near-empty `build.c` per unconstrained module acceptable (every
  module is a real target, CLAUDE.md's "modules carry no build.c" yields), or do we want auto-synthesis of
  a default module target so trivial modules need no file at all?
- **Framework location (§11).** Framework code stays in `tools/build/` with templates in `lib/build/`
  (recommended, per prd/10), or move the whole framework under `lib/build/`?
- **Selector richness (§5).** Is the `Mel_When` field set complete (platforms, backend, gpu, runtime,
  config), or do we want negation (`WHEN_NOT`) and host/target as selectable axes too?
- **Variant identity for caching (§4/§7).** Confirm the full `Mel_Variant` (including `host`) is the cache
  key discriminator, so host and target builds of one TU coexist without collision.
- **CLAUDE.md.** The "Every target has a `build.c` module; modules do not" line (and the module-layout
  paragraph) needs updating once §3/§8 land — recorded here as a recommendation per the session-writeup
  rule, not edited as part of this plan.
