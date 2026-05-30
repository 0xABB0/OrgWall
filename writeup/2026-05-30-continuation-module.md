# Continuation: Stackless Coroutines as Reified Frames

This implements the `continuation` module from `design/continuation.md` (now moved to
`modules/continuation/spec.md`), including the libclang source-to-source codegen that the spec
deferred to the not-yet-built build system. Per direction, the codegen lives *inside* the module,
the module is excluded from the `nob` framework on every platform, and it is authored to the golden
spear with its own self-contained build/test driver.

## What was delivered

A complete, working continuation primitive and its compiler:

- **ABI** (`include/continuation/abi.h`): `Mel_Cont_Suspended` (a `bool`), integer state sentinels,
  the layout-hash type. The frame is caller-allocated, fixed-size, pointer-free POD.
- **Macro surface** (`include/continuation/cont.h`): `mel_cont`, `mel_cont_yield`, `mel_cont_await`,
  `mel_cont_return`, with a codegen mode (clang-detectable annotated sentinels) and a portable
  editor mode.
- **The transform** (`codegen/continuation_gen.c`): a real C→C compiler over libclang's read-only C
  API. It splices the user's source byte-ranges verbatim and synthesizes only the dispatch scaffold,
  emitting a Duff's-device state machine. Handles straight-line, `if`/`else`, `while`, `for`,
  `do`/`while`, arbitrary nesting, automatic lifting (including loop induction variables) via
  conservative liveness, value/void yield and return, and `mel_cont_await` composition with yield
  forwarding. Every unsupported construct is a hard `file:line` rejection.
- **Self-contained driver** (`build.c`, `nob`-style): builds the tool against libclang, runs codegen
  on fixtures, golden-diffs the output, and compiles + runs the differential, snapshot, and
  rejection suites. `./build {tool,gen,bless,test}`.
- **Tests** (all green): 5 differential fixtures vs hand-written oracles (`sum_to`, `countdown`,
  `classify`, `repeat_sum`, `relay`), a `memcpy` snapshot round-trip, golden frame layouts, and 4
  rejection fixtures asserting the right `file:line`.
- **Docs**: `readme.md` (full), `decisions.md` (every decision + every spec deviation with
  rationale), `todo.md` (honest gaps).

## Why it works — the keystone

libclang's C API gives an AST with token-accurate ranges but no rewriter. So the transform never
re-prints user C: it copies the user's expressions/types/macros byte-for-byte and authors only the
`switch`, the state assignments, and the lifted-local rewrites. Loops need no special lowering —
a `case` label left *inside* the preserved loop body, jumped to from the top-level switch (Duff's
device), re-enters mid-iteration with frame-backed induction variables. This is exactly what
hand-rolled protothreads can't do, and the reason the codegen exists.

Two libclang subtleties bit and were resolved: macro-argument identifiers collapse to the call site
under *expansion* locations (fixed by using *spelling* locations for references), and compound-stmt
range ends point one-past the `}` (fixed by scanning back to the brace). Suspension call bytes are
located by paren-scanning the source rather than trusting end-of-range offsets.

`await` introduced a genuine bootstrap problem (the parent body names a sibling's generated frame
type, which doesn't exist at parse time). Solved with an iterative two-phase parse: synthesize all
frame structs, force-include them, re-parse to a fixpoint, then transform.

## Kludges and debt (the bar is zero; full confession)

- **A generated header is emitted**, contradicting the spec's "no generated header." The three
  desiderata (no header / stack-allocatable frames with real `sizeof` / liveness-derived fields) are
  mutually unsatisfiable; correctness won. This is the single largest deviation. (decisions.md #4)
- **Liveness is conservative and kill-free** — correct (never under-lifts) but may over-lift a dead
  local, so frames are not provably minimal. The spec's verification mode (asserting every
  non-lifted read is dominated by an assignment with no intervening suspend) is not built. (todo)
- **Multi-declarator lifted decls are untested** (`i64 a=1, b=2;`). They rely on per-declarator
  comma-expression fallout with no fixture. Should be tested or rejected; for now, one declarator
  per statement. (todo)
- **Lifted array-with-initializer is rejected** rather than lowered to a `memcpy`. Honest, not
  silent, but a real capability gap.
- **`await` is intra-invocation only** (parent and child must be generated together) and forwards a
  child's yields only when channel types match canonically, else routes them to a discard slot.
- **Conservative rejections over-fire slightly**: any `goto`/label with any suspension is rejected
  (not just those that cross one); any VLA before the last suspension is rejected. Safe, documented.
- The single-TU tool was kept whole rather than split into the spec's component files — a structure
  choice, not debt, but noted.

No silent miscompiles were left anywhere: the failure mode for every unhandled construct is a loud
build error with `file:line`.

## CLAUDE.md suggestions (recommendations only — not applied)

- The repo `CLAUDE.md` "Inside a module folder" section prescribes `public/ private/ src/ meta/`,
  but **every module on disk uses `include/<name>/` + `src/`**. The prescription should be updated to
  match reality, or the modules migrated. I followed the de-facto idiom (MEL-CODE-005).
- Consider documenting an **unconditional `mel_build_exclude_module`** (no platform axis). Excluding a
  module "always" currently requires a `MEL_PLATFORM_*` loop.
- The codegen-registration step is still "undocumented; halt and query." This module's `build.c` is a
  working reference for what continuation codegen needs from the future build system; worth pointing
  the build-system PRD at it.

## Suggestions

- **Build-system handoff**: wire `mel_build_generate_continuation` + a runner hook mtime-gated on the
  tool and marked sources, mirroring `run_enum_str_codegen`. `todo.md` lists the exact contract.
- **Serialization handoff**: the layout hash is emitted but unconsumed; the save/netcode module
  should own a versioned frame envelope that checks `MEL_CONT_LAYOUT_HASH_*` before loading.
- **Repo hygiene**: `enum_str_gen.c` and `continuation_gen.c` now share substantial libclang
  boilerplate (prefix detection, builtin-include discovery, SDK path, diagnostics). When a third
  libclang tool appears, factor a tiny `tools/codegen/libclang_common.h`.
