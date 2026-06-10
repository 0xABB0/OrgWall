# Build rigor: global per-variant graph, fingerprints, loud failures

## Work done

Implemented the build-rigor design (spec folded into `modules/build/platforms.md` per MEL-SPEC-002), four parts in parallel agents, integration by hand. The `build.c` authoring API is source-compatible; two additive codegen calls.

- **Global per-variant graph** (`emit.c`, `graph.c`). One `build/<platform>[-sim]-<config>/build.ninja` covering every discovered target, `builddir` set so `.ninja_log`/`.ninja_deps` are per-variant (previously shared at repo root by all targets and variants — lost log entries disabled ninja's command-hash detection, the root cause of "flags changed but nothing rebuilt"). Phony alias per target; the requested target's outputs are passed to ninja explicitly; legacy repo-root ninja state is removed on first run.
- **Object namespacing** (`emit.c`). Objects live under `obj/<target>/`; `..` glob segments sanitize to `__`. Exposed by the global graph: sibling test targets sharing `tools/test/src/runner.c` collided on one object path (under per-root emission they silently overwrote each other's object, possibly with different flags).
- **Variant lock** (`util.c`). Advisory lock on `build/<variant>/.lock` (flock; win32 CreateFileA no-share loop); a second `nob` of the same variant waits with a message. Verified live.
- **Loud failures**. `build.c` compile errors stream and abort discovery (were `/dev/null` + silent target disappearance); unknown dep/codegen tool, unavailable dep in the requested closure, zero-matching source globs on a library/executable: all fatal. Unavailable targets elsewhere in the graph are skipped with a one-line count.
- **Discovery depfiles** (`discovery.c`). Loader `.so` staleness from a real clang depfile (second `clang -MM` pass; `-MMD` on a multi-TU compile overwrites per TU) instead of a fixed input list.
- **Third-party fingerprints** (`thirdparty.c`). `.thirdparty-built` now records builder kind, full configure args, toolchain, config, prebuilt url/lib; mismatch wipes and reconfigures; match still runs `cmake --build`/`make` (inner build system owns vendored-source staleness). Prefix flag injection moved out of the build functions into one `mel_inject_thirdparty` pass at emission so flags are identical regardless of requested root and never duplicate across re-emissions.
- **Codegen dependency tracking** (`build.h`, `api.c`, `emit.c`, tools). `.h`-arg sniffing heuristic deleted; `mel_codegen_input()` declares explicit inputs, `mel_codegen_depfile()` marks tools that write `<output>.d`. `coro_gen` and `enum_str_gen` now emit depfiles from their libclang inclusions; `coro_gen` also stops rewriting its input header at fixpoint (was bumping its own input's mtime every run).
- **Per-directory compile databases** (`compdb.c`, `driver.c`). `./nob compdb [target] [platform…]` writes `compile_commands.json` per target directory (closure-scoped with a target; all targets without), plus `modules/build/compile_commands.json` and a root DB holding only `nob.c`. clangd's upward search picks the nearest DB.
- **win32 resource as a ninja edge** (`package.c`, `emit.c`). `app.rc` written at emission (content-compared, mtime-stable), `llvm-rc` runs as a tracked `rc` edge with icon/manifest as inputs — previously an unconditional subprocess at every emission. win32 `ar` rule deletes the archive first (stale members lingered on win32).
- `ninja -t cleandead` after successful builds reaps outputs that left the graph.

Verified on macOS: cold + no-op + header-touch + flag-change-both-directions builds of `coro-example`; full `hello-world-gui` (155 edges) + package; `nob test` single test; wasm/ios emission; live lock contention; per-dir compdb (96 DBs repo-wide, zero foreign entries).

## Kludges

- `cleandead` result is ignored and its output suppressed; a failure there is invisible.
- Host tools (`build/host/`) are shared across variants and not covered by the per-variant lock; two `nob` runs of *different* variants can still race on a host tool.
- The variant directory omits `arch` and `gpu`: `--gpu=vulkan` vs `metal` builds share `build/macos-debug/` outputs and log — rebuilds are *correct* (command hashes differ) but the configurations thrash each other's objects. Pre-existing in target outdirs; now also true of the shared graph dir.
- First build after migration resettles a few codegen edges once (plain-depfile edges record on first run), then exact no-op. Benign, observed, not fixed.
- win32 paths untested this session: the `cmd /c del $out_win` ar rule, the `rc` edge, and `mel_lock_dir`'s CreateFileA loop compile under `#ifdef _WIN32` but have not run on win-pilot.
- The POSIX lock fd is intentionally never closed (lock lifetime = process); win32 handle likewise.
- Third-party fingerprint covers args/toolchain/config, not vendored source contents; `cmake --build`/`make` no-op runs add latency to every build whose closure has third-party deps.
- `run_tests` re-emits the (identical) global ninja file once per test target.
- Migration costs paid once per variant: all third-party reconfigure (old stamps say `ok`), all objects recompile (new `obj/<target>/` paths). Dead legacy per-target `build.ninja` files and old `obj/` trees remain on disk; nothing cleans them.
- compdb arg parsing suppresses the generic "unknown platform" error for the compdb verb so a target may follow a platform — a parse carve-out in `driver.c`.
- Discovery's dep scan is a second clang invocation per loader rebuild rather than a single combined pass.

## CLAUDE.md suggestions (recommendations only)

- Build commands: `./nob compdb [target] [platform …]` now writes per-directory DBs; the root DB covers only `nob.c`.
- Worth stating for multi-agent work: concurrent `nob` runs of the same variant now serialize on `build/<variant>/.lock` — "sometimes the build breaks because multiple agents are working" should no longer apply to same-variant object races.
- The emitted graph lives at `build/<variant>/build.ninja`, not in target outdirs.

## Suggestions

- Fold `gpu`/`arch` into the variant directory name (and target outdirs) so configurations stop sharing output paths.
- A `nob clean [target|variant]` verb; also a one-shot janitor for the legacy per-target `build.ninja`/`obj/` trees.
- Cover host tools with a lock (or per-variant host outdirs).
- Run the win32 path on win-pilot (commit + push, build a gui app there) before relying on the ar/rc changes.
- Consider a vendored-source manifest hash in the third-party fingerprint for fully hermetic third-party staleness, paying the `make` no-op cost only on mismatch.
