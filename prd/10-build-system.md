# Build System Architecture

## Problem Statement

The current build system uses `nob.c` to compile native and Android NDK code, but each app duplicates platform scaffolding: every app has its own `android/` directory with full Gradle setup, its own Win32 `.rc` and `.manifest`, its own macOS Info.plist (when added), and so on. With two apps and one platform that is already painful. With six platforms (Win32, Linux, macOS, iOS, Android, web) and ten apps, that becomes sixty per-app scaffolding directories that drift independently. Apps cannot share infrastructure cleanly; library-level updates do not propagate; new platforms require touching every existing app.

Equally bad, the existing layout has no notion of stages, callbacks, or extensibility for build steps. There is no way for an app to register custom code-generation as part of its build, no way to declare conditional dependencies, no caching of build artifacts beyond what the C compiler does on its own, and no propagation of build flags from dependencies to consumers ("everything that depends on me also wants -lm").

The build system needs to be a real framework — not a templater — with stages, extension points, content-addressed caching, and a coherent target model where everything from `melody` itself to third-party libs to apps is a target.

## Solution

The unit of the build system is the **target**. A target is anything the build system knows how to produce: the `melody` library, a third-party library, an application, an application flavor, or a meta-target aggregating others. Every target has a `build.c` next to it that defines the target via callbacks registered into named stages.

Four stages:

- **configure** — generate per-target project files (Gradle scaffolding, xcconfig, MSBuild props, web bundle config). Templates live in `lib/build/<platform>/` and are expanded with per-target config.
- **compile** — compile sources to object files. Sub-stages `fetch_sources` (gather what to compile) and `compile_source` (run the compiler) are exposed individually.
- **link** — link objects into the final artifact (.a, .so, .dll, .exe, .app, .apk, .wasm).
- **package** — produce the distributable form (signing, bundling, archive creation, web asset minification, deploy-ready output).

Each stage is a callback chain. Framework defaults run first, user callbacks layer on. Multiple callbacks per stage are registered additively. Self-filtering inside callbacks handles platform-specific behavior — there is no DSL for filters, just `if (ctx->platform == MEL_PLATFORM_ANDROID) { ... }` in the callback.

Build invocation is root-only: `./nob configure <target> [platform]`, `./nob compile`, `./nob link`, `./nob package`, plus convenience `./nob build` and `./nob run`. No invocation from subdirectories.

Content-addressed caching is universal: every compiled object, every linked artifact, every packaged result is keyed by a hash of its inputs (source bytes, all included headers, compiler flags, dependency artifact hashes). Switching debug↔release reuses cached artifacts instantly when nothing has changed.

No ejection. Per-target platform-specific scaffolding is always generated fresh into `build/<target>/<platform>/`. Apps customize only via three mechanisms:

- Declarative configuration through C struct fields in `build.c`
- Imperative logic through additional callbacks in `build.c`
- Additive file-based overrides at `apps/<name>/<platform>/` (sources, resource fragments, manifest fragments) that the build system merges into generated projects using platform-native mergers

The library is the single source of truth for scaffolding. App updates inherit library improvements automatically.

## Implementation Decisions

A target is defined by its `build.c` exporting a single function `bool project(Mel_Build_Target* t)`. Inside `project()`, the target sets its name, kind (library / application / third-party / meta), supported platforms, dependencies, public/private properties, and registers callbacks for stages.

Dependencies propagate properties. A target marks build flags, include directories, and preprocessor defines as `public` or `private`. Public properties become part of any target that depends on this target; private ones are limited to the target itself. The mechanism is the same as CMake's `PUBLIC` / `PRIVATE` distinction.

Callback registration is additive and ordered. Framework defaults register first; user callbacks register through `mel_build_on_configure`, `mel_build_on_compile`, etc., and run in registration order after the defaults. Suppressing a default is done by setting a flag in `project()` that the default callback consults.

The configure stage uses templates from `lib/build/<platform>/`. Templates contain placeholders that are expanded with values pulled from the target's accumulated configuration (from C struct fields and the union of all configure callbacks). Generated projects are written to `build/<target>/<platform>/`.

For Android specifically: the shared Gradle logic lives in `lib/build/android/melody.gradle.kts`. Per-target generated Gradle files are short — they apply the shared file and pass per-target values. Apps' additive Java sources, resources, and manifest fragments at `apps/<name>/android/` are wired into the generated project via Android Gradle's existing source-set, resource-merger, and manifest-merger features. The merge logic is delegated to Android's tools, not reimplemented.

For Apple platforms (macOS, iOS, visionOS): shared xcconfig at `lib/build/apple/melody.xcconfig`. Per-target generated Info.plist is produced by a small plist-merger we own (no platform-native merger exists for plist). App additive Obj-C/Swift sources and partial Info.plist fragments at `apps/<name>/<platform>/` are merged in.

For Win32: shared MSBuild .props at `lib/build/win32/melody.props`. Per-target generated .rc and .manifest from templates. App additive sources at `apps/<name>/win32/`.

For web: shared bundler config and HTML/JS templates at `lib/build/web/`. App overrides at `apps/<name>/web/`.

Caching is content-addressed throughout. Every compiled object's filename in `build/cache/` is `<hash>/<object_name>.o`, where the hash is computed over: the source file bytes; the byte contents of every header the compiler reports as included via `-MD`; the compiler binary's path and version string; the full set of relevant compile flags; the hashes of every dependency artifact transitively. Build directories under `build/<target>/<platform>/<config>/` are populated by hard-linking or copying from the cache. Switching configurations is a cache lookup, not a recompile, when inputs are unchanged.

Cache garbage collection: `./nob cache gc` removes cache entries older than a configurable threshold that are not referenced by any current target build.

Backend selection (which UI toolkit a target targets) is part of `build.c`: `mel_build_backend(t, MEL_PLATFORM_WIN32, "win32")`, `mel_build_backend(t, MEL_PLATFORM_MACOS, "cocoa")`, etc. The backend choice gates which per-framework widget implementation files participate in the build (PRD 06).

App-defined custom widgets ship per-backend files in `apps/<name>/<backend>/` or `apps/<name>/<platform>/<backend>/`. The build system picks them up alongside the app's main sources.

A `build.c` failure (parse error, missing dependency, callback returning false) fails the whole build at that target with a clear error. No silent fallthrough.

## Testing Decisions

A good test of the build system runs an end-to-end build of a small target, asserts the resulting artifact exists and is well-formed (executable runs, library has expected symbols), and asserts cache behavior (rebuild without source changes is no-op, switching configurations reuses cached objects).

Modules under test: `nob.c` itself, in-process where feasible (target discovery, callback registration, dependency resolution, cache key computation), and end-to-end via actual builds.

Prior art: `nob.c` has existing test coverage for its own helpers (file walking, command spawning); cache-and-stage logic is new.

## Out of Scope

- IDE project generation as a first-class output (Visual Studio .sln, Xcode .xcodeproj at top level). IDEs that consume Gradle/CMake-like outputs work today via the generated platform projects in `build/`.
- Cross-target parallelism beyond what individual stages already enable (the compile stage parallelizes across translation units via the existing `nob` infrastructure).
- Distributed build (remote workers, build farm). The cache is local-only.
- Build artifact signing keys management. Signing is invoked from package callbacks; key storage is out of scope.
- Migration of existing apps from per-app `android/`, `win32/` directories to the new model. PRD documents target shape.

## Further Notes

The `build.c` per target is itself a C source file compiled by `nob` as part of build orchestration. `nob` compiles each target's `build.c` as a translation unit linked against `lib/build/` library code, runs `project()` to gather the configuration, then proceeds with stages. This means `build.c` has full C — conditionals, loops, function calls — without inventing a DSL.

The "no eject" rule is load-bearing for keeping the library a single source of truth. If apps could check in their generated platform projects, they would drift, and the library could not evolve without breaking them. The merge mechanism (TOML-less; just C config + additive file overrides) is the safety valve that replaces ejection.

The backend selection in `build.c` interacts with the widget model (PRD 06): a target's chosen backend determines which per-backend widget files compile. This is the only build-level coupling between the build system and the widget model.
