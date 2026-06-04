# mel_depends_when — WHEN-gated dependencies

## Work done

Added a `Mel_When`-gated dependency edge to the build framework, mirroring the
selector that `mel_sources`/`mel_link`/`mel_includes` already accept, then
converted the two consumers that had proved its need.

### API

    void mel_depends_(Mel_Target* t, const char* name, Mel_When when);
    #define mel_depends(t, name)            mel_depends_(t, name, ALWAYS)
    #define mel_depends_when(t, name, when) mel_depends_(t, name, when)

`mel_depends` is now `mel_depends_when(…, ALWAYS)`; every existing call site is a
drop-in (the macro keeps the old two-argument spelling). The signature mirrors
`mel_sources_`/`mel_link_` exactly: a stored `Mel_When` per declaration.

### How the gating is implemented

The dependency record changed from a bare name (`Mel_StrVec deps`) to
`Mel_Dep { const char* name; Mel_When when; }` (`Mel_DepVec deps`).

The single choke point is `mel_topo_closure` (graph.c), which every consumer
already calls to build the per-variant dependency closure. It now takes the
resolved `const Mel_Variant* v`; its recursive `visit` skips any edge whose
`when` does not `mel_when_match(v)`. Because that closure (`order`) is the *sole*
driver of:

- include/cflag/define propagation (`mel_gather_compile`, resolve.c),
- link-flag propagation (`mel_gather_link`, resolve.c),
- third-party **prepare** (cmake/autotools/prebuilt — `mel_prepare_thirdparty`,
  thirdparty.c, iterates `order`),
- source/object emission and static-library linking (`emit_one`, emit.c, iterates
  `order`),
- compdb availability (`closure_available`, compdb.c),

a non-matching dependency leaves the closure entirely on that variant: none of
its `MEL_PUBLIC` includes/links reach the dependent, and its prepare step never
runs. On a matching variant it participates exactly as before.

All four `mel_topo_closure` call sites (resolve.c ×2, emit.c, compdb.c) thread
their in-scope `v`. The autotools `CPPFLAGS`/`LDFLAGS` dependency-prefix gather
in thirdparty.c (`build_autotools`) also reads `dep.name` and honours `dep.when`,
so a gated-off dep contributes no `-I`/`-L` there either.

`platforms.md` documents the new primitive and its one caveat (below).

### Conversions

- **modules/gui/build.c** — replaced the raw
  `mel_cflags(lib, MEL_PRIVATE, WHEN(linux), "-Imodules/log/include")` workaround
  with `mel_depends_when(lib, "log", WHEN(.platforms = MEL_ON(LINUX)))`. The linux
  XCB backend (`src/xcb/{backend,widgets}.c`) includes `<log/log.h>` and calls
  `mel_log_*`; previously the header came from the hand-rolled `-I` and the
  symbols resolved by transitive accident. After conversion the linux hello-gpu
  link line carries `modules/log/build/linux-debug/liblog.a` directly through the
  gui→log edge — the dependency is now real, not incidental.

- **modules/gpu/build.c** — gated both Vulkan thirdparty edges from unconditional
  `mel_depends` to `mel_depends_when(…, WHEN(.gpu = "vulkan", .platforms =
  MEL_ON(LINUX)))`:
  - `vulkan-headers`
  - `vulkan-loader-stub`

  They are no longer pulled on macos/win32/android/wasm or on metal/d3d12/webgpu.
  (macOS `--gpu=vulkan` links Homebrew's `-lvulkan`/MoltenVK directly per the
  pre-existing `WHEN(vulkan, MACOS)` link flags; it never needed the linux
  loader-stub, and the gpu-vulkan suite proves it.)

## Verification (broad — build framework is high blast radius)

- **macOS gpu-vulkan** — `DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1
  ./nob test gpu-vulkan macos --gpu=vulkan` → **48 passed, 0 failed, of 48**.
- **macОS metal** — `./nob configure hello-gpu macos --gpu=metal`: ninja emitted,
  **0** `vulkan-loader-stub`/`libvulkan`/`vulkan-headers`/`-lvulkan` references.
- **macOS webgpu** — `./nob configure hello-gpu macos --gpu=webgpu`: ninja emitted
  (Dawn prebuilt fetched), **0** vulkan refs, 18 webgpu refs.
- **linux vulkan** — `./nob build hello-gpu linux --gpu=vulkan`: **full build
  linked completely** (`[166/166] LINK …/hello-gpu`, exit 0). Ninja carries the
  loader-stub `-L`, `-lvulkan`, `-Ithird-party/vulkan-headers/include`,
  `-Imodules/log/include`, and `liblog.a` in the link — nothing dropped by the
  gating.
- **win32 d3d12** — `./nob configure hello-gpu win32 --gpu=d3d12`: ninja emitted,
  **0** vulkan refs.
- **wasm** — `./nob configure hello-gpu wasm`: ninja emitted, **0** vulkan refs.
- **compdb** — `./nob compdb macos linux`: both variants resolved; the linux gui
  xcb compdb entry carries `log/include` (now via the real edge).

No platform regressed.

## Limitation reported (the build()-vs-prepare boundary)

`mel_depends_when` gates the dependency **closure** (include/link propagation and
the third-party **prepare** step). It does **not** gate a third-party target's
eager work done in its own `build()` body, because discovery runs every `build.c`'s
`build()` exactly once, variant-agnostically, before any closure exists — `build()`
has no `Mel_Variant`.

`third-party/vulkan-loader-stub/build.c` generates `libvulkan.so` (shells out
`zig cc`) inside `build()`, not via the prepare path. Consequently the stub `.so`
is still generated at discovery on every platform/gpu (verified: a `--gpu=metal`
macos configure still produces `third-party/vulkan-loader-stub/build/libvulkan.so`).
What the gating *did* fix on non-linux: the stub is no longer **linked or
included** into any dependent, and `vulkan-headers` no longer propagates. The
residual cycle-theft is one idempotent `zig cc` on first discovery (subsequent
runs short-circuit on the existing `.so`); the `#ifndef _WIN32` guard already
keeps win32 out of the `realpath`/generation path.

I deliberately did **not** invent a new framework pass to move the stub generation
into the variant-aware prepare step — registering a new pass is flagged
"undocumented — halt and query." Flagging it for Gabbo (Open questions).

## Kludges

- **None in the shipped framework/build.c changes.** The gating reuses the
  existing closure machinery; no special-casing, no fixed arrays, no enums, no
  comments added.
- **Verification-only, never committed (all gitignored):** the worktree is a fresh
  checkout with no third-party build artifacts, and this host lacks
  `automake`/`aclocal-1.17`, so a from-scratch `gmp`/`mpfr`/`sdl3` autotools build
  fails. To run the configures/builds I copied the already-built
  `third-party/{gmp,mpfr,sdl3}/build/<platform>-<config>/` prefixes (with their
  `.thirdparty-built` stamps) from the main checkout into the worktree. These live
  under gitignored `build/` dirs and are not part of the change. They are a host
  environment workaround, not engine debt.

## CLAUDE.md suggestions (recommendations only)

- Document the `nob` bootstrap explicitly: a fresh worktree has no `nob` binary
  (gitignored). The one-liner is `clang -std=c23 -g -Imodules/build -o nob nob.c`
  (the exact command `nob.c`'s `NOB_REBUILD_URSELF` uses). Worth a line in the
  Build section so agents in worktrees don't hunt for it.
- Note that third-party `build/` artifacts do **not** propagate into worktrees;
  cross/autotools deps must be (re)built there, which needs `automake` on the host.

## Suggestions

- **Move loader-stub generation into the prepare path.** Give third-party targets
  a `Mel_When`-gated, variant-aware generator hook (a shell command run inside
  `mel_prepare_thirdparty`, like cmake/autotools/prebuilt), so the loader-stub's
  `zig cc` runs only when the stub is actually in a matching closure. That closes
  the residual discovery-time theft cleanly and would make `mel_depends_when` gate
  the stub end-to-end. This is the proper fix; it needs Gabbo's sign-off because it
  adds a build pass.
- Consider a `mel_unavailable`-style loud assert if a `mel_depends_when` names a
  target absent from discovery on a matching variant — currently the unknown-target
  error only fires when the edge is actually traversed, so a typo'd gated dep stays
  silent on non-matching variants (acceptable, but worth a deliberate decision per
  MEL-CODE-007).
