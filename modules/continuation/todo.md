# todo

Known gaps and future work, honestly enumerated (MEL-ENGINE-VIII). None of these are silent: every
unsupported construct that could miscompile is a hard `file:line` rejection, not a wrong frame.

## Transform

- **Multi-declarator lifted decls work but lack a fixture.** `i32 a = 1, b = 2;` correctly becomes
  `__f->a = 1, __f->b = 2;` (comma expression) with both lifted — verified by hand, not yet a
  committed test. Mixed lifted/non-lifted in one declarator, and pointer declarators (`int *a, b`),
  are still unverified. Lock it in with a fixture and probe the mixed/pointer cases.
- **`break`/`continue` across a suspension work but lack a fixture.** They bind to the user's loop
  and are spliced verbatim; verified by hand. Add a fixture so it can't regress.
- **`-Wunused-parameter` on the dead `static inline`.** A continuation with an unused parameter warns
  in every includer (editor mode). Harmless, but noisy under `-Wall -Wextra`; suppress by tagging the
  editor-mode signature or its params.
- **Precise liveness (minimality).** Liveness is conservative and kill-free, so frames may carry a
  dead local that happens to be referenced after a suspension on an unrelated path. Correct, but not
  minimal. A real backward dataflow with def/kill over a basic-block CFG would tighten frames; the
  spec also wants a verification mode asserting every read of a non-lifted local is dominated by an
  assignment with no intervening suspend edge.
- **Lifted array with initializer is rejected.** Could be supported by lowering `T a[N] = {…};` to a
  `memcpy` from a compound literal instead of an assignment.
- **`mel_cont_await` flattening.** A depth-N await chain resumes through N parent dispatchers per
  step (O(depth)). Correctness-neutral and documented; a flattening pass is possible if it bites.
- **Cross-file await.** A parent and the child it awaits must be in the same codegen invocation so
  the parent learns the child's signature. A persisted signature index would lift this.

## Testing

- **More fixtures:** nested loops with a suspension in the inner loop; `continue`/`break` across a
  suspension; struct-typed yields; pointer-to-external-memory locals (legal, should not be flagged).
- **Differential fuzzing** against randomly generated straight-line equivalents would catch liveness
  miscompute more aggressively than the curated oracles.

## Build-system handoff

This module deliberately stops at its own `build.c`. The spec's *Build integration* section is the
contract the future build and codegen-registration modules must satisfy:

- a runner hook (`run_continuation_codegen`) mtime-gated against the tool and the marked sources,
  appending the generated `.c` to the target's sources;
- a `mel_build_generate_continuation(t, "path/to/marked.cont.c")` build API backed by a
  `File_Paths continuation_sources` field;
- a `nob.c` `NOB_GO_REBUILD_URSELF_PLUS` entry so a tool edit triggers a rebuild;
- the Android edge path (`emit_android_edges`) opt-in.

The in-module `build.c` is the executable reference for what that integration must reproduce.

## Serialization handoff

The per-continuation `MEL_CONT_LAYOUT_HASH_*` is emitted but unused here. The owning save/netcode
module consumes it to refuse or migrate a frame whose layout drifted. A snapshot helper
(`mel_cont_frame_blit` or similar) and a versioned envelope belong there, not here.
