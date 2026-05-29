# Enum-to-string codegen via libclang

## Work done

Added a build-integrated code generator that synthesizes `str8 <EnumType>_to_string(<EnumType>)`
from annotated enums, replacing hand-written `switch` tables. Motivated by
`apps/display-gui/src/main.c`, which carried six near-identical switches that had to be
edited by hand whenever a `display.h` enum gained a constant.

Design (final): the to-string is **declared as a real prototype in the enum's own
header** via a macro, and the codegen emits **only the implementation `.c`** — no
generated header to `#include`. This keeps the declaration LSP-visible and avoids the
"generate a file then include it" pattern entirely.

- `modules/reflect/` — new header-only module. `include/reflect/enum.h`:
  - `MEL_ENUM_TO_STRING(EnumType)` / `MEL_ENUM_TO_STRING_DEFAULT(EnumType, default)` —
    expand to `str8 EnumType_to_string(EnumType)` carrying an
    `__attribute__((annotate("mel:enum_to_string[:default]")))`. On non-clang the
    prototype still stands; the annotation drops.
  - `MEL_STR(label)` overrides one constant's string; `MEL_SKIP` omits one. Both sit on
    the enumerator (before any `= initializer` — GNU attribute placement).
- `tools/codegen/enum_str_gen.c` — libclang (C API) tool. Parses an in-memory umbrella TU
  that `#include`s the named headers, finds annotated `FunctionDecl`s, recovers each enum
  from the parameter's canonical type, reads per-constant `mel:str`/`mel:skip` from the
  `EnumConstantDecl` children, dedups functions by name and constants by value, and emits
  `enum_to_string.generated.c`. Each return is a plain literal-backed struct literal
  (`(str8){ (u8*)"HDMI", 4 }`) — length precomputed at generation, so the output needs no
  `S8`/string-module dependency, only the enum's own header. Default label = constant
  spelling minus the enum's common prefix, verbatim.
- Build API: `mel_build_generate_enum_str(t, "display/display.h", ...)` (variadic, in
  `build.h`/`build.c`), backed by `File_Paths enum_str_headers` on the target
  (`internal.h`).
- `tools/build/runner_codegen.c` (new TU, included from `runner.c` before
  `runner_ninja.c`): discovers libclang, builds the tool once per run, and
  `run_enum_str_codegen` resolves the header spellings on the include path, mtime-gates
  against the headers and the tool, runs generation, and appends the generated `.c` to the
  target's sources. Hooked into `emit_target_edges` after source resolution, before cc
  edges. No include-path mutation needed — the declarations come from the real header.
- `nob.c` — added `tools/build/runner_codegen.c` to the `NOB_GO_REBUILD_URSELF_PLUS` dep
  list (see kludges: its absence caused a stale-driver failure mid-development).
- Converted the motivating code: annotated the six enums in `display.h` (labels preserved
  exactly), deleted the six static functions in `main.c`, opted the app in
  (`apps/display-gui/build.c`), and rewrote the call sites to
  `Mel_Display_Connector_to_string(...)` etc. A one-line `LBL` macro
  (`#define LBL(x) ((const char*)(x).data)`) bridges the `str8` return back to the `%s`
  /`strncat` call sites.

Verified: `./nob build display-gui` generates, compiles, links, runs; a no-op rebuild is
inert; touching `display.h` regenerates. Generated labels/lengths/defaults match the
original strings exactly.

## Decisions (Gabbo's)

Four-way ask: libclang C API; `__attribute__((annotate))`; strip-prefix-verbatim default;
build artifact regenerated on header change. Follow-up directive: no generated header —
declarations live in the header via a macro, return type `str8`, name `<EnumType>_to_string`;
codegen emits the `.c` only (generated headers play badly with LSPs).

## Kludges / debt

- **`nob` self-rebuild dep list is hand-maintained.** `nob.c` enumerates every
  `runner_*.c` for `NOB_GO_REBUILD_URSELF_PLUS`; a new included TU that isn't listed won't
  trigger a driver rebuild, so edits silently run against the stale `nob` binary (this bit
  me — the old 2-output tool argv ran against the new tool and failed to parse). Added
  `runner_codegen.c` to the list. A glob over `tools/build/runner_*.c` would remove the
  footgun.
- **`LBL` relies on `str8` NUL-termination.** The generated labels are string literals, so
  `(const char*)s.data` is a valid C string and `%s`/`strncat` work unchanged. Correct for
  every label today, but it is an assumption; a non-literal `str8` would truncate/overrun.
  A `%.*s` (`(int)s.len, s.data`) print path would be airtight at the cost of touching each
  format string.
- **Homebrew-LLVM host dependency, macOS-centric.** Xcode ships `libclang.dylib` but not
  `clang-c/Index.h`; the tool builds against a full LLVM install (`/opt/homebrew/opt/llvm`
  or `$MEL_LIBCLANG_PREFIX`), mirroring the macOS-Vulkan Homebrew prefix. Errors loudly if
  absent. The SDK/builtin-include plumbing is `#if defined(__APPLE__)` plus a best-effort
  builtin-include glob; Linux/Windows hosts are untested.
- **Android app path bypasses codegen.** `emit_target_edges` early-returns into
  `emit_android_edges` for Android apps, so generation never runs there. Fine today; wire
  into the Android edge path when an Android target opts in.
- **`no-bare-malloc` lint warns on the tool.** `tools/codegen/enum_str_gen.c` uses raw
  `malloc`/`realloc` — it can't reach the engine arena (doesn't link the engine).
  Consistent with the status quo (`tools/build/build.c`, `runner_graph.c` already warn).
- **Value-collision handling is soft.** Aliased enumerators (same value) drop to one `case`
  with a stderr note, not a hard error. No display enum aliases today.

## CLAUDE.md suggestions (recommendations only — not applied)

`CLAUDE.md` gained a stub `## Codegen / This repo uses a custom codegen step.` Proposed
body, pointing at the fuller docs in `platforms.md`:

> ## Codegen
> Targets may have `str8 <EnumType>_to_string(<EnumType>)` synthesized from annotated
> enums (`<reflect/enum.h>`: `MEL_ENUM_TO_STRING`/`MEL_STR`/`MEL_SKIP`), opted in per
> target with `mel_build_generate_enum_str`. The to-string is declared in the enum's own
> header; a libclang pass (`tools/codegen/enum_str_gen.c`, wired into the compile stage)
> emits only the implementation `.c` under `build/.../generated/`. Requires a full LLVM
> install on the build host — `brew install llvm` or `$MEL_LIBCLANG_PREFIX`. Fuller
> documentation lives in `tools/build/platforms.md`.

## Suggestions

- **Glob the `nob` rebuild dep list.** Replace the explicit `runner_*.c` enumeration with a
  directory glob so new framework TUs are tracked automatically.
- **Scope `no-bare-malloc` out of tooling.** Add to `.sgrules/no-bare-malloc.yml`:

      ignores:
        - "tools/**"
        - "build/**"

- **Promote the strings into the `display` module.** The labels are reasonable canonical
  names. Annotating in `display.h` and opting the `melody` library in (not the app) would
  export `Mel_Display_Connector_to_string` to all consumers (MEL-ENGINE-IX). Left in the
  app to keep the change scoped.
- **Generalize the harness.** The annotate-attribute + libclang pattern extends to flag-set
  decoders (would subsume the `fields_str` bitfield formatter still hand-written in
  `main.c`), enum count/min/max, and struct reflection — one tool, more `mel:` verbs.
- **Host-portable libclang discovery.** Prefer `llvm-config --includedir/--libdir/--prefix`
  with the Homebrew prefix as fallback, so Linux/Windows hosts work without
  `$MEL_LIBCLANG_PREFIX`.
