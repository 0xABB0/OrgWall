# Melody Build System — From-Scratch Design

A ground-up reimplementation. There is **no backwards compatibility to preserve**: the build
*system* is large (~3.2K lines of runner), but its *usage* is tiny — thirteen `build.c` files, each
a handful of declarative calls. Rewriting those thirteen alongside the runner is cheaper than any
migration, so this design owes nothing to the old implementation. No parallel path, no parity gate,
no flip-and-delete. We delete the old `runner_*.c` and write the new framework in its place.

> **Naming caveat.** Identifiers here are provisional, chosen to carry the design, not frozen.
>
> **Authority.** The retired `prd/10`/`prd/04` notes are gone from the repo; this document does not
> lean on them. Its only authorities are the Ten Commandments (`docs/commandments.md`) and the
> module-layout convention already stated in `CLAUDE.md`.

---

## 0. Governing principle — simple surface, powerful core

The whole design serves one law: **MEL-ENGINE-II — hide thy complexity, not thy power.** Simplicity
is measured on the *consumer* surface — what a person writes in a `build.c` — not on the runner. An
unconstrained module's `build.c` is three lines; the runner behind it carries the entire platform
matrix, the cross-axis variant resolution, and the host-tool subgraph. The simple path and the
powerful path are the same path: a constrained target merely adds selector-gated lines, never a new
concept.

Two further commandments bind concretely here. **MEL-ENGINE-IX (compose):** four property verbs over
one predicate, not a verb per axis-combination — power from multiplication, not multitude.
**MEL-ENGINE-I (don't shy from the hard problem) read inversely:** we do not re-implement what the
platform already affords. Ninja is the executor; `ccache` is the cross-variant object store. We own
neither, and write no scheduler or content cache to replace them.

---

## 1. The paradigm we keep

- **`build.c` is an ordinary C program.** It links `libmelbuild.a`, exports `bool project(Mel_Build_Target *t)`,
  and constructs a target through a small API. No DSL, no parser. This is the one thing the old system
  got unambiguously right (it is Zig's `build.zig` model), and it stays.
- **Ninja is the executor.** The runner emits one `build.ninja` per invocation and shells out to it.
  Ninja owns dispatch, bounded parallelism, the console pool, and dependency-order. We do not own an
  incrementality model — emitting a graph for a mature executor is less code and more correct than a
  hand-rolled scheduler.
- **`ccache` is the cross-variant object store, auto-detected.** If `ccache` is on `PATH`, compile
  rules are prefixed with it; objects are then shared across variants and branches for free, with zero
  owned cache code. Absent it, per-variant build dirs (`build/<platform>/<config>/…`) already make a
  debug↔release flip cheap — each variant keeps its own current object tree, so a switch-back is a
  Ninja no-op. There is **no owned content-addressed cache** and no `cache gc` verb.

---

## 2. The target model — everything is a target, and the kind set is open

A target is declared by a `build.c` exporting `project()`, discovered and `dlsym`'d. The kind is not a
closed enum but a **registered descriptor** carrying a name and per-stage default callbacks:

```c
typedef struct {
    const char *name;
    void      (*register_defaults)(Mel_Kind_Builder *b);
} Mel_Kind_Desc;

MEL_API Mel_Kind mel_register_kind(const Mel_Kind_Desc *desc);
MEL_API void     mel_kind_default(Mel_Kind_Builder *b, Mel_Stage stage, Mel_Build_Stage_Fn fn);
```

The framework registers the built-ins at startup — `library`, `application`, `third_party`, `module`,
`host_tool` — each wiring its stage defaults through `mel_kind_default`. A user adds a kind by the same
call, so a custom kind is not a special case in the runner; it is one more registration. `host_tool`'s
only distinction is that it is implicitly host-rebound (§3).

```c
mel_name(t, "hello-gpu");
mel_kind(t, MEL_APP);
```

---

## 3. Axes & variant — a threaded struct, with a working host rebind

Five axes — platform, config, UI backend, gpu backend, runtime — are correct. The old defect was that
they lived in globals bound once per invocation, so the whole graph built for one variant. We move them
onto a **struct threaded through resolution**:

```c
typedef struct {
    Mel_Platform platform;
    Mel_Config   config;
    const char  *backend;
    const char  *gpu;
    const char  *runtime;
    bool         host;
} Mel_Variant;
```

One variant is requested per invocation. But a dependency edge may **rebind** the `host` axis, and this
rebind is *built now*, not deferred — because host-tool dependencies (§7) require it:

```c
mel_depends(t, "melody");            // inherits the consumer's variant
mel_depends_host(t, "asset-baker");  // this subgraph resolves with host = true
```

A `host_tool` target is implicitly host-rebound. The orchestrator therefore resolves the graph as a set
of **(target, variant)** pairs: a target may legitimately appear twice — once for the host (a generator),
once for the target (shipping code). Build dirs and Ninja rule names are keyed by the full variant, so
the two never collide.

> **Deferred hook — reflect-meta.** The host-built reflection/codegen *driver* (the `reflect`/`clang`/
> `codegen` modules, the host-compiled-`meta/`-as-driver semantics of §8) is **not** built in this
> design. What *is* built is the substrate it will stand on: the variant struct, the working `host`
> rebind, and host-tool deps. Adding reflect-meta later is new targets on an unchanged core — no rework.

This same generalization absorbs Android's per-ABI sub-axis (today a side-channel `Cross` descriptor) as
another variant field, removing a special case.

---

## 4. The selector — the central ergonomic win

One predicate, four verbs. The selector is a designated-initializer compound literal; an unset field
means "any", so a selector reads as exactly the axes it constrains:

```c
typedef struct {
    uint32_t     platforms;
    const char  *backend;
    const char  *gpu;
    const char  *runtime;
    Mel_Config   config;
    bool         has_config;
} Mel_When;

#define MEL_ON(p)  (1u << (MEL_PLATFORM_##p))
#define WHEN(...)  ((Mel_When){ __VA_ARGS__ })
#define ALWAYS     ((Mel_When){ 0 })

MEL_API void mel_cflags_(Mel_Build_Target *t, Mel_Visibility vis, Mel_When when, ...);
MEL_API void mel_defines_(Mel_Build_Target *t, Mel_Visibility vis, Mel_When when, ...);
MEL_API void mel_includes_(Mel_Build_Target *t, Mel_Visibility vis, Mel_When when, ...);
MEL_API void mel_link_(Mel_Build_Target *t, Mel_Visibility vis, Mel_When when, ...);

#define mel_cflags(t, vis, when, ...)   mel_cflags_(t, vis, when, __VA_ARGS__, NULL)
#define mel_defines(t, vis, when, ...)  mel_defines_(t, vis, when, __VA_ARGS__, NULL)
#define mel_includes(t, vis, when, ...) mel_includes_(t, vis, when, __VA_ARGS__, NULL)
#define mel_link(t, vis, when, ...)     mel_link_(t, vis, when, __VA_ARGS__, NULL)
```

The umbrella's old four-verb dialect collapses to one verb each, and a *set* of platforms is now
expressible without repetition:

```c
mel_link(t, MEL_PUBLIC, WHEN(.gpu = "vulkan"), "-lvulkan");
mel_link(t, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "-framework", "Metal");
mel_cflags(t, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-I/opt/homebrew/include");
mel_link(t, MEL_PUBLIC, WHEN(.runtime = "emscripten"), "--use-port=emdawnwebgpu");
```

The four verbs over the one selector replace the entire `_on`/`_on_gpu`/`_on_runtime` × `{public,private}`
family — forty-odd functions become four. Source exclusion folds into the same predicate, and module
availability is the module's own gate rather than the consumer's:

```c
mel_exclude_source(t, WHEN(.platforms = MEL_ON(ANDROID) | MEL_ON(LINUX)), "surface.m");
mel_unavailable(t, WHEN(.platforms = MEL_ON(WEB)));
```

This is what makes the surface *feel* like Zig: a small composable verb set over one predicate
(MEL-ENGINE-IX), not a verb per axis-combination.

---

## 5. Properties & propagation

CMake-style `PUBLIC`/`PRIVATE` visibility, propagated transitively along dependency edges: a target
inherits the public cflags, defines, includes, and link flags of everything it depends on, gated by each
property's selector against the consumer's resolved variant. This is kept verbatim in spirit — it is the
mechanism that lets the umbrella's flag pile *disperse* (§8): a framework no longer carries `-framework Cocoa`,
the `display`/`gpu` modules that need Cocoa carry it, and it reaches the app through propagation.

---

## 6. The stage model — an open registry with sub-stages

Stages are entries in an **ordered registry**, not a closed enum. Built-ins — `configure`, `compile`,
`link`, `package` — register with ordering constraints; a user inserts a custom stage the same way:

```c
typedef struct { const char *name; const char *after; } Mel_Stage_Desc;
MEL_API Mel_Stage mel_register_stage(const Mel_Stage_Desc *desc);
```

Each stage runs **layered callbacks**: the kind's default first (suppressible), then user callbacks in
registration order. The four built-in `on_*` verbs generalize to one, since stages are now open:

```c
MEL_API void mel_on(Mel_Build_Target *t, Mel_Stage stage, Mel_Build_Stage_Fn fn);
MEL_API void mel_suppress_default(Mel_Build_Target *t, Mel_Stage stage);
```

`compile` carries two **sub-stages** — `fetch_sources` (gather the translation-unit list) then
`compile_source` (emit per-TU Ninja rules). Sub-stages are themselves registry entries, so codegen and
asset pipelines slot in by ordering (§7), never as a bolt-on.

---

## 7. Extensibility — the four required extensions, all first-class

This design must buy all four extension points, and each is an ordinary use of §2/§3/§6, not a carve-out:

- **Codegen as a first-class participant.** A pass registers a `fetch_sources` callback. It may declare a
  host-tool dependency (its generator), run that generator over the ctx, and append generated TUs:
  ```c
  static bool gen(Mel_Build_Context *ctx) {
      const char *tool = mel_ctx_host_tool(ctx, "reflect-driver");
      run(tool, mel_ctx_out_dir(ctx));
      mel_ctx_add_source(ctx, path_join(mel_ctx_out_dir(ctx), "reflect.generated.c"));
      return true;
  }
  ```
  The old 132-line `runner_codegen.c` island dissolves into this: a `fetch_sources` callback over the
  normal target/variant machinery.
- **Custom stages / sub-stages** via `mel_register_stage` with an `after` ordering constraint (§6).
- **Custom target kinds** via `mel_register_kind` with their own stage defaults (§2).
- **Host tools as deps** via `mel_depends_host` (§3) — the dep resolves and builds under the host variant,
  and its artifact path is handed to consumer stage callbacks through `mel_ctx_host_tool`.

The four compose: a custom kind may register a custom stage whose default is a codegen pass that runs a
host tool. No special pleading anywhere (MEL-ENGINE-IX).

---

## 8. Discovery, the registry, and availability-as-error

Discovery walks `apps/*/build.c`, `modules/*/build.c`, `third-party/*/build.c`, and host-tool `build.c`,
compiles each to a DLL, `dlsym`s `project()`, and registers the target.

**Every module is a target (uniform).** Each `modules/<m>/` carries its own `build.c` declaring
`kind = MODULE`, its availability, and its dependency edges. Boilerplate is bounded by defaults: an
unconstrained module is three lines, its sources auto-discovered from `src/`, its API exposed from
`public/`. Only a module that constrains availability or declares non-trivial deps writes more.

```c
bool project(Mel_Build_Target *t) {
    mel_name(t, "math");
    mel_kind(t, MEL_MODULE);
    return true;
}
```

```c
bool project(Mel_Build_Target *t) {
    mel_name(t, "server");
    mel_kind(t, MEL_MODULE);
    mel_unavailable(t, WHEN(.platforms = MEL_ON(WEB)));
    mel_depends(t, "mongoose");
    return true;
}
```

Availability is a property *of the module*, declared once and inherited by every consumer — retiring the
old backwards model where `modules/build.c` excluded `server`/`gui`/`gpu` from the *consumer* side. The
resolver **errors** when an explicitly-requested module is unavailable for the active variant (depending
on a module for a platform it does not support is a build error, needing no `#if` in consumer code), and
prunes only transitive deps reachable solely through an unavailable *optional* one. The umbrella `melody`
target becomes a thin aggregate depending on every available module; its old platform-flag pile disperses
to the modules that own those flags (Cocoa → `display`/`gpu`, `-lasound` → `midi`, the Win32 libs →
`platform`).

**Directory convention.** Module roots are `public/` (API on the dependents' include path), `private/`
(internal headers), `src/` (target-axis implementation, platform-chain gated as today), and `meta/`
(metaprogram code). This matches the layout `CLAUDE.md` already documents and supersedes the current
`include/`+`src/` split. The folder *is* the signal — no flag. The `meta/` directory's
host-compiled-as-driver semantics are the deferred reflect-meta hook (§3); the directory is recognized
now, its driver behavior built later.

The platform-chain source gating (`macos→apple→posix`, the `*.win32.c`/`.posix.c`/`.m` suffix rules,
the additive `src/<backend>/`, `src/<gpu>/`, `src/<runtime>/` dirs) is kept exactly — it is sound and
needs no change.

---

## 9. Incrementality

Stated in §1 and restated here as the single source of truth: **Ninja executes; `ccache` stores objects
across variants; we own neither.** The runner's job is to emit a correct `build.ninja` for the resolved
(target, variant) graph and invoke it. Per-variant build dirs isolate configs; `ccache` (when present)
shares unchanged objects across them and across branches. No owned cache, no scheduler, no `gc`.

---

## 10. CLI & entry

Kept verbatim, because it is good and users know it:

```
./nob build|run|debug|test|configure|compile|link|package <target> [platform[:backend[:runtime]]]
                                                  [--release|--debug] [--gpu=<id>] [-- <test args>]
```

Empty axis fields fall back per axis (`web::wasi`); root-only invocation. No new verbs — host-tool
subgraphs build transparently before the consumer's compile stage, invisible at the CLI (MEL-ENGINE-II).

---

## 11. Code layout of the new framework

Framework code and its scaffolding templates live under **`lib/build/`** — the runner translation units
archived into `libmelbuild.a`, the templates (Gradle, plist/xcconfig, `.rc`/manifest, web bundle config)
as data under `lib/build/<platform>/`, separating code from data. The `nob` driver at the repo root
includes the runner TU as today. Translation units, by concern, mirroring a clean split but smaller for
the deletions (no cache, no owned scheduler):

`target` (model, kind registry), `variant` (axis resolution, host rebind), `select` (the `Mel_When`
engine), `graph` (discovery, topo, (target,variant) orchestration), `stage` (the stage/sub-stage
registry and layered-callback execution), `compile` (`fetch_sources`/`compile_source`), `link`, `ninja`
(the `build.ninja` emitter), `platform` (chain/suffix gating), and the package backends `apple`,
`android`, `win32`, `web`.

---

## 12. Build order

No migration — this is construction order, sequenced by dependency:

1. **Core, host-only, end-to-end.** Target model + kind registry (§2), the `Mel_Variant` struct with the
   `host` field present (§3), discovery, the `Mel_When` selector engine (§4), the stage registry (§6),
   and the `ninja` emitter for `compile`/`link`. Rewrite all thirteen `build.c` onto the new API. Goal:
   `./nob build <app>` builds and links on the host platform.
2. **The platform matrix.** Platform-chain gating (§8), the backend/gpu/runtime axes, web configuration,
   and the package backends (`apple`, `android`, `win32`, `web`).
3. **Modules-as-targets.** Per-module `build.c` with declared availability and deps; availability-as-error
   in the resolver; disperse the umbrella's flag pile; synthesize the `melody-test` target over module
   targets.
4. **The extensibility surface.** Custom stages, custom kinds, codegen-as-`fetch_sources`, and host-tool
   deps — which lands the working `host` rebind and the (target, variant) double-build (§3, §7).
5. **`ccache` launcher.** Auto-detect and prefix compile rules (§1, §9).

The directory convention (§8, `public/private/src/meta`) is mechanical and may land any time after step 3.

---

## 13. The one open layout point

`CLAUDE.md`'s "Every target has a `build.c` module; modules do not" line is contradicted by §8 (every
module now carries a `build.c`), and its module-layout paragraph already names `public/private/src/meta`
which §8 adopts. Per the session-writeup rule this is recorded as a recommendation, not edited here: the
`build.c` line needs to flip to "every target, modules included, carries a `build.c`."
