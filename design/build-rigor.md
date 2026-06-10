# Build Rigor

Make incremental builds trustworthy: no silent staleness, no silent graph errors, safe under concurrent invocations. The `build.c` authoring API does not change.

## Failure modes in the implementation

- `mel_discover_dir` compiles `build.c` with `mel_run_quiet`; on error the directory's targets silently vanish from the graph (`modules/build/discovery.c`).
- `emit_one` / `mel_gather_compile` failures print and continue; ninja then runs a partial graph. A library that matches zero sources is dropped from `produced` and silently omitted from dependents' link lines (`modules/build/emit.c`).
- Unknown codegen tool warns and continues (`modules/build/emit.c`).
- Every root target emits its own `build.ninja` containing the full dependency closure. The same object files appear as outputs in many ninja files; `.ninja_log`/`.ninja_deps` live in the repo-root cwd, shared by every target and variant. Two concurrent `nob` runs race on the same objects and corrupt the shared log; a lost log entry disables ninja's command-change detection, so flag changes stop triggering rebuilds (mtime-only fallback).
- Third-party builds are gated by a boolean `.thirdparty-built` stamp; changes to vendored sources, cmake/autotools args, toolchain, or prebuilt URL never rebuild (`modules/build/thirdparty.c`).
- Codegen inputs are guessed by sniffing args ending in `.h` and probing `-I` paths; any other input read by the tool is untracked (`modules/build/emit.c`).
- The win32 `ar` rule does not delete the archive first; removed objects linger as stale members. No reaping of dead outputs after renames/deletes on any platform.
- The discovery `.so` rebuild check uses a fixed input list (`build.c`, `api.c`, two headers); headers included by a `build.c` are untracked.

## Goals

- Any change to sources, flags, `build.c`, codegen inputs, or third-party inputs rebuilds exactly its dependents.
- Graph-construction errors abort the run with the underlying error visible (MEL-ENGINE-VIII).
- Concurrent `nob` invocations are safe (multiple agents work on this repo simultaneously).

## Design

### One graph per variant
Emit a single `build/<platform>[-sim]-<config>/build.ninja` covering every discovered target, with `builddir` set to that directory so `.ninja_log`/`.ninja_deps` are per-variant and adjacent to the graph. `nob build <t>` emits, then invokes ninja on `<t>`'s outputs. Output paths (`<target>/build/<variant>/…`) are unchanged; only the graph and ninja state centralize. This removes duplicate edges, makes command-change detection reliable, and gives every target of a variant one consistent view.

### Variant lock
An advisory file lock on the variant directory. A second `nob` for the same variant waits and says so. Different variants proceed in parallel.

### Loud failures
- Discovery compile errors stream stderr and abort discovery.
- Unknown dep, unknown codegen tool, unavailable dep, or a library whose source globs match nothing: fatal, named, non-zero exit.
- `mel_emit_and_build` aborts if any `emit_one` in the closure fails.

### Third-party fingerprint
Replace the boolean stamp with a fingerprint file recording: builder kind, args, toolchain (`cc`, triple), config, prebuilt URL. Mismatch reruns configure. The build step (`cmake --build` / `make`) runs every time — staleness of vendored sources is delegated to the third-party's own build system, which no-ops when fresh. `--install` reruns only when the build did work.

### Codegen dependencies
Drop the `.h`-sniffing heuristic. `mel_codegen` gains explicit input declaration; additionally a tool may write `$out.d` (Makefile syntax) and the codegen rule declares it as `depfile`. Tools that read headers via cflags emit the depfile themselves.

### Dead output reaping
Run `ninja -t cleandead` after successful builds (valid once the per-variant log exists). Win32 `ar` rule deletes the archive before creating it, matching POSIX.

### Discovery `.so` tracking
Compile loader `.so`s with `-MMD` and gate rebuilds on the emitted depfile instead of a fixed input list.

## Phasing

Independent, in implementation order:
1. Loud failures (no prerequisites).
2. Win32 `ar` fix (no prerequisites).
3. Third-party fingerprint (no prerequisites).
4. Codegen depfiles + explicit inputs (no prerequisites).
5. Discovery depfiles (no prerequisites).
6. Per-variant graph + builddir + lock + cleandead (subsumes the ninja-state fixes; largest change: `emit.c`, `driver.c`).
