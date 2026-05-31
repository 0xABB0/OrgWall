# Build System Rewrite — 2026-05-31

Replaced the old `tools/build/` runner (~3.2K lines) with a new framework under
`modules/build/`. The build system is now a small core that discovers per-target `build.c`
scripts, resolves one `(platform, arch, config, backend, gpu, runtime)` variant, propagates
flags along an explicit dependency graph, and emits `ninja`. `nob` self-rebuilds and
unity-includes the framework.

## Work done

**Core (`modules/build/`)**
- `build.h` — the consumer contract. `void build(Mel_Build *b)` constructs **named artifacts**
  (`mel_add_library/executable/third_party/host_tool/test`); a folder is a location, an artifact
  is built-from-sources-and-exposed-to-dependents. The `_on_*` combinatorial API is gone: four
  property verbs (`mel_cflags/defines/includes/link`) over one `Mel_When` selector, plus
  `mel_sources/exclude_source` (glob `*`/`**`), `mel_depends[_host]`, `mel_unavailable`,
  `mel_manifest`, `mel_cmake/configure`, `mel_codegen`.
- `api.c` — stateless setters mutating the runner-allocated target; identical whether compiled
  into a target's `.so` or into `nob`. `__VA_OPT__` on the NULL-terminated variadic macros.
- `select.c` — `Mel_When`↔`Mel_Variant` match + `*`/`**` globber (replaces the scrapped shadow
  chain; modules gate axis dirs explicitly).
- `discovery.c` — compile each `build.c`+`api.c`→`.so`, `dlopen`, call `build()`, collect
  artifacts (multi-artifact per file). Quiet compile; the resolver surfaces needed-but-missing.
- `graph.c` — dep resolution, cycle detection, topo closure.
- `resolve.c` — native variant defaults; variant-gated transitive propagation (each include path
  resolved relative to the declaring target's dir).
- `toolchain.c` — per-platform `Mel_Toolchain` (cc/ar/sysroot/exe-ext/triple), arch-derived.
- `thirdparty.c` — cmake **and** autotools executors (build-stamped; dep-prefix injection via
  CPPFLAGS/LDFLAGS); inject prefix `include`/`lib` as public flags.
- `emit.c` — ninja emitter: per-TU compile (host vs target rules), archive, link, codegen rules,
  per-module `build/<platform>-<config>/` layout, `ar` rm-f'd. Drives configure/compile/link/
  package stages.
- `package.c` — macOS/iOS `.app`, win32 `.rc` resource, android gradle `.apk`; template +
  manifest-substitution + per-app `apps/<app>/<platform>/` override.
- `driver.c` — all verbs.

**Proven end-to-end (macOS host)**
- 25 source modules + `continuation` migrated; all 6 apps build and bundle as `.app`.
- 6-platform cross-compilation of compute apps: macos/linux(zig)/win32(zig)/android(NDK)/ios/wasm
  (wasm runs under node).
- Codegen owned by modules: `enum_str_gen` (in `reflect`) and `continuation_gen` (in
  `continuation`), two different CLIs through one generic `mel_codegen` host-tool primitive.
- Three third-party modes: source amalgamation (`sqlite3`/`mongoose`), cmake (`sdl3`), autotools
  (`gmp`/`mpfr` from source — Homebrew kludge removed; linux + android cross proven).
- Packaging: macOS `.app`, iOS `.app`, win32 `.exe` (icon/version/manifest), **android `.apk`**
  (full GUI app: NDK `.so` + gathered gui java + gradle), web mechanism (compute `.js`).
- `arch` sub-axis (`--arch`, cross-arch on same platform), asm `.S` gated by `(arch, format)`.
- All verbs: `build/run/debug/test/configure/compile/link/package`; `./nob test` green (5/5
  continuation tests).

## Kludges / debt (MEL-ENGINE-VIII — confessing all)

- **win32 cross-gmp blocked** on this host: `zig cc -target windows` trips gmp's libtool into
  MSVC mode (`lib` not found); the Homebrew `x86_64-w64-mingw32-gcc` fails gmp's configure
  (`-mabi=ms`/X32). Deferred to a real Windows machine (your call). win32 *resources* and
  *compute* cross both work; only the gmp-dependent GUI link is blocked.
- **wasm GUI blocked**: gmp won't autotools-build under emcc, and `app`/`reactor` lack wasm
  sources. The web `.html`+shell path is wired; only the build is gated.
- **Cross GUI = cross gmp**: every GUI app pulls `gui→math→mpfr→gmp`. Where the toolchain is sane
  (NDK, zig-linux) it works; mingw/emcc gmp is the wall.
- `-dead_strip` is Apple-only; linux/win32 don't dead-strip yet (no `-ffunction-sections` +
  `--gc-sections`).
- Android native lib name hardcoded `libmelody.so` (matches the fixed `MelodyActivity.loadLibrary`).
- Codegen header deps are best-effort (resolves `.h` args against `-I`; no depfile, so transitive
  header changes don't retrigger). Self-modifying codegen (continuation rewrites its fixture) is
  handled with order-only deps, but the pattern is fragile.
- `compile`/`link` verbs both map to "build, no package" (no true object-only stop). No `clean`
  verb. `configure` emits ninja only.
- Host detection assumes the build tool runs on macOS.
- `arch` handles `arm64`/`x86_64`; `wasm32` is nominal.
- Third-party builds are stamp-gated (`.thirdparty-built`), not content-hashed; `rm -rf build`
  forces a full (slow) gmp/mpfr/SDL rebuild.
- Android `package` runs `gradle assembleDebug` (downloads AGP over the network; system gradle
  9.3.0 throws Gradle-10 deprecation warnings against AGP 8.13.2 — builds, but pin a wrapper).
- **Not built** (deferred from design, not regressions): `ccache` launcher; the open kind/stage
  *registry* for custom kinds/stages (kinds are fixed constants); a synthesized `melody-test`
  aggregate (`test` runs discovered `mel_add_test` targets); continuation's golden-file and
  reject (expect-fail) checks (only the behavioral diff tests are wired).

## CLAUDE.md suggestions (recommendations only — not edited here)

- "Every target has a `build.c` module; modules do not" is now false. Every module/app/third-party
  carries `build.c` (except `modules/build/` itself, which IS the framework). Flip the line to
  "every target — modules included — carries a `build.c`; `modules/build/` is the framework and is
  skipped by discovery."
- Document the new authoring API (the `mel_add_*` + selector-verb surface) where the old
  `mel_build_*` API was described.
- The "## Build commands" block is still accurate (all verbs restored).
- Note that `tools/build/` is gone; framework lives in `modules/build/`, templates in
  `modules/build/{macos,ios,win32,android,web}/`.

## Suggestions

- **Pin a gradle wrapper** (8.x) in `modules/build/android/` so the apk build is reproducible and
  not at the mercy of the host's system gradle.
- **gmp/mpfr cross**: for win32, try `ABI=64` + explicit `CFLAGS`/binutils, or vendor a known-good
  mingw; for wasm, consider a pure-C bignum or gating `math`'s mpfr features behind a backend so
  non-native GUI apps don't hard-depend on gmp.
- **ccache**: auto-prefix compile rules when present — near-free given the ninja emitter.
- **Depfile for codegen** (have generators emit `-MF`) to make codegen incrementally correct.
- **`clean` verb** + content-hash stamps for third-party.
- Consider a real **object-only `compile`** stop and a synthesized **`melody-test`** aggregate.
- The session is one commit's worth of a large, coherent change; the macOS story is complete and
  the cross/compute matrix is proven — good point to land it and tackle win32-gmp on real hardware.
