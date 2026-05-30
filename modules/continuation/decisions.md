# Decisions

Every decision taken building this module, with rationale. Deviations from [spec.md](spec.md) are
flagged **[DEVIATION]**. The spec remains the statement of intent; where implementation revealed a
sharper shape, this file is authoritative on the as-built reality (Gabbo sanctioned "refine where
sharper, document every deviation").

## Structure & house rules

1. **Module layout follows the de-facto idiom, not the prescribed one.** Every real module on disk
   uses `include/<name>/` + `src/` (+ `readme`, optional `test/`); the `public/private/meta` layout
   in the repo `CLAUDE.md` is contradicted by every module. Idiomatic wins (MEL-CODE-005). Here:
   `include/continuation/`, `codegen/`, `test/`, `build.c`. Recorded as a `CLAUDE.md` recommendation
   in the session writeup, not acted on unilaterally.

2. **No code comments anywhere.** The global directive forbids comments; all rationale lives in
   markdown (this file, `readme.md`). The exclude line added to `modules/build.c` is therefore
   bare; its explanation is here (decision 15).

3. **Single-TU codegen tool.** `codegen/continuation_gen.c` is one file, mirroring
   `tools/codegen/enum_str_gen.c`. The spec's "Components" decomposition (abi / macros /
   transform-straightline / transform-loops / compose / serialize) is a *dependency* decomposition,
   honored as internal sections, not a file-per-component split. Splitting a working compiler into
   TUs sharing libclang state buys little and risks regression.

## ABI

4. **The frame struct + prototype are injected into the user's own header; there is no separate
   generated header to include.** A stack-allocatable frame needs its complete, liveness-derived
   layout in scope at the call site, and only codegen can produce that — so *some* generated
   declaration must reach the caller. The first cut emitted a standalone `<name>.gen.h`; Gabbo
   rejected it (rightly) as bad ergonomics — it forces callers to `#include` a file that does not
   exist before codegen. The resolution matches the spec's literal words ("emitted into the user's
   own header"): codegen edits the header where the continuation is written, replacing a managed
   `/* >>> mel_cont generated frames >>> */ … /* <<< */` region (idempotent) with the frame struct,
   layout-hash macro, and resume prototype, placed just before the first `mel_cont(`. Callers
   `#include` the header they already own; the frame type appears right above the continuation.
   Only the resume *definition* lands in a sibling `<name>.gen.c` (compiled/linked, never included).
   In editor/normal compiles the body lowers to a discarded `static inline`, so the header is
   includable anywhere at zero cost and clangd still analyses a real body. The one irreducible gap
   vs C++ coroutines: a brand-new continuation has no frame type until its first codegen run (the
   file exists, so includes resolve — only the type is briefly unknown, exactly like a yet-to-
   compile C++ TU). Injecting frames into the header also resolves the `await` bootstrap for free
   (siblings sharing a header see each other's frames).

5. **[DEVIATION] `Mel_Cont_Suspended` (a `bool`), not a `Mel_Cont_Status` enum.** MEL-CODE-001
   forbids enums; pending/done is a genuine binary outcome, so a `bool` is the precise type, not a
   closed set masquerading as one. `resume` returns `true` while suspended, `false` when complete,
   which reads cleanly as `while (resume(&f, &y)) use(y);`.

6. **State sentinels are integer indices, not an enum.** `MEL_CONT_STATE_START` = 0,
   `MEL_CONT_STATE_DONE` = -1. The state is a true resume *index* (a counter), so integer constants
   are the honest type, not disguised enum members.

7. **[DEVIATION] The yield channel is an out-parameter and its type is inferred.** The spec sketches
   a generic `Mel_Cont_Yield*` and an optional 4th macro argument typing the channel. In reality the
   yield type is per-continuation, so it is **inferred from the yielded expressions' canonical type**
   (canonical so `i32` and the promoted `int` of `base + 100` unify; consistency across yields is
   enforced, else `file:line` rejection). The out-parameter keeps the yielded value *off* the frame
   (it lives only at the suspension instant), keeping frames minimal (MEL-ENGINE-III). The optional
   4th macro argument is accepted but documentary only.

8. **Parameters live in the frame, caller-initialized.** `resume` takes no argument channel, so a
   referenced parameter is a frame field the caller sets before the first resume (`f.n = 10;`).
   Formally identical to a lifted local. Unreferenced parameters are omitted (minimal frame).

## The transform

9. **Duff's-device state machine via source-range splicing.** The resume body is the user's source
   spliced verbatim, wrapped in `switch (__f->state) { case 0: … }`, with each suspension replaced by
   `{ … __f->state = K; return true; case K:; }`. Loops need no special lowering: a `case` inside a
   preserved loop body, jumped to from the switch, re-enters mid-iteration. This is the keystone that
   makes the read-only libclang C API sufficient.

10. **Reference offsets use the *spelling* location; call/structure offsets use the *expansion*
    location.** A macro-argument identifier (e.g. `acc` inside `mel_cont_yield(acc)`) collapses to
    the macro call site under expansion locations; its spelling location is the real source byte. A
    lifted reference is rewritten by replacing `[spelling_start, spelling_start+strlen(name))` with
    `__f-><name>`, guarded by a byte-for-byte name match (mismatch → "not spliceable" rejection).

11. **Suspension call bytes are located by paren-scanning the source**, not by trusting libclang
    end-of-range offsets (which proved to be one-past-the-token for compound statements and
    unreliable for the macro-expanded call's closing paren). From the call's expansion start we scan
    for the balanced `(`…`)` and the trailing `;`. The yielded/returned/awaited value is whatever
    sits between the parens (kept in place to receive its own reference rewrites); `has_value` is
    "the parens are non-empty".

12. **Conservative kill-free liveness.** A local is lifted if a reference reaches it after a
    suspension on any path, including the loop back-edge (any reference inside a loop enclosing the
    suspension). No def/kill analysis: over-lifting only grows the frame and is always correct;
    under-lifting (the catastrophic case — reading a dead local) cannot happen. The spec explicitly
    blesses conservative bias as the mitigation for liveness miscompute. Precise read/write/kill
    liveness is a minimality refinement (see todo).

13. **Lifted declarations become assignments.** `T x = e;` → `__f->x = e;`; an aggregate initializer
    `T x = {…};` → `__f->x = (T){…};` (compound-literal assignment, legal in C; bare `__f->x = {…}`
    is not); an uninitialized `T x;` → `(void)0;` (the frame is already zero-init by the caller). The
    for-init declarator `for (T i = 0; …)` rewrites in place to `for (__f->i = 0; …)`.

## await / composition

14. **[DEVIATION] `await` surface kept as `mel_cont_await(child_frame)`; the bootstrap is solved by
    iterative in-place injection.** The parent body names `Mel_Cont_Frame_child`, which does not
    exist at parse time. The tool parses, injects all frame structs into the header (decision 4),
    re-reads and re-parses the now-augmented header, and repeats until the injected region reaches a
    fixpoint (converges in depth+1 iterations; capped at 8). Because the frames land in the same
    header, a parent and the children it awaits see each other with no extra include. Rejections are
    suppressed during these quiet iterations and enforced on a final clean pass.

15. **`await` semantics: drive-to-completion with conditional yield forwarding.** On each child
    suspension the parent re-suspends. If parent and child share a canonical yield type, the child's
    value forwards through the parent's `__f_out`; otherwise it lands in a per-await discard slot
    `__await_out_K` in the parent frame. A pure-relay parent (no own yields) adopts the child's yield
    type so forwarding works. The child frame is embedded in the parent frame and always lifted, so
    the whole composition stays one self-contained, snapshotable POD. The two continuations must be
    in the same codegen invocation (the parent needs the child's signature).

## Build & packaging

16. **[DEVIATION] The marked `*.cont.h` is both the codegen input and the includable interface; no
    non-clang macro fallback.** The spec's non-clang fallback ("degrade to a forward declaration")
    is unnecessary and self-contradictory at a definition site with a body. Instead the header is
    the one artifact: codegen reads it (codegen mode) and injects frames into it; callers include it
    (editor/normal mode), where `mel_cont(...) { … }` lowers to a discarded `static inline`
    (`__attribute__((unused))`) so it compiles and is clangd-analysable at zero runtime cost. The
    portable runtime surface is the injected header + the generated `.gen.c` (plain C, compiles
    anywhere). `cont.h` has exactly two modes: `MEL_CONT_CODEGEN` (clang-detectable sentinels +
    annotate attribute) and editor mode.

17. **Excluded from the framework on every platform.** There is no unconditional module-exclude in
    the build API, so `modules/build.c` loops `MEL_PLATFORM_*` calling `mel_build_exclude_module_on`.
    The module is built and tested only by its own `build.c`. This honors the instruction to author
    it to the golden spear rather than fit it to the discovery rules, and keeps a `main()`-bearing
    `build.c` and a libclang tool out of the engine link.

18. **The codegen tool lives in-module (`codegen/`); the macros live in `<continuation/cont.h>`, not
    `<reflect/continuation.h>`.** Per the instruction that the module be maximally self-contained.
    `reflect/` and `tools/codegen/` are untouched. The spec's *Build integration* section (runner
    hook, `mel_build_generate_continuation`, `nob.c` dep-list entry) is therefore the contract the
    *future* build and codegen-registration modules must satisfy — this module exists in part to
    inform their design.

19. **Goldens are raw, unformatted output: the injected `.cont.h` plus the `.gen.c`.** Deterministic
    from the tool with no external `clang-format` dependency or version skew. The tool always runs on
    a pristine scratch copy of each fixture, so injection starts from a clean region every time and
    the result never accumulates. The spliced indentation is cosmetically rough but correct.

20. **`.cache/` is the build scratch dir (not `build/`).** The driver binary is `build`, which would
    collide with a `build/` output directory.
