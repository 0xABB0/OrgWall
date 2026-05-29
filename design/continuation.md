# Continuation — Stackless Coroutines as Reified Frames

A continuation is a stackless coroutine whose suspended state is a reified delimited
continuation: a flat data value holding a resume index and every local that survives a
suspension. The name is exact — the runtime object *is* a delimited continuation, and the
codegen that produces it is defunctionalization of continuations over a CPS-explicit
suspension set. Prefix `mel_cont_`.

## The promise

Write an ordinary-looking C function. Mark it. Get back a struct you own and a `resume`
function that drives it to its next suspension point — no stack, no heap, no scheduler,
serializable, relocatable, runnable inside an audio callback.

## Essence — the POD frame

Everything is downstream of one property: a suspended continuation's entire state is plain
owned data. Three capabilities are corollaries of that single property, not separate
features (MEL-ENGINE-IX):

1. **Runs where fibers are forbidden.** `resume` is a plain function call against caller-owned
   memory — legal inside the audio realtime callback, which permits no fibers, no reactor,
   no waiting, no allocation. No other Melody primitive can suspend and resume there.
2. **Snapshots.** A flat frame `memcpy`s to disk (save), rewinds (rollback netcode), or
   replays deterministically. A machine stack cannot — it embeds return addresses and
   self-referential pointers.
3. **Survives hot-reload.** The frame holds no code pointers (state is an integer index), so
   a reloaded module resumes a frame its prior incarnation minted, given stable layout.

A use needing none of these three does not need a continuation; it needs a fiber. The
primitive must not grow into a general-purpose coroutine — that re-imports the function-
coloring problem stackful fibers exist to avoid. Suspension is permitted only in the marked
body, never through a helper call.

## The frame contract — normative core

**The frame is caller-allocated, fixed-size, and self-contained.** The codegen computes a
compile-time frame size and exposes it; the caller provides the storage — stack, embedded in
a struct, arena, or pool. The runtime never allocates a frame (MEL-ENGINE-III). Secret
allocation would forfeit the realtime capability outright.

A frame holds exactly:

- `i32 state` — the resume index. `0` is not-yet-started; positive values are suspension
  points; a terminal sentinel is completed. It is an integer, never a code pointer — this is
  what makes the frame relocatable and reload-safe.
- one slot per local live across at least one suspension point. Locals never live across a
  suspension are reborn each resume and stay off the frame, keeping frames minimal.
- the started argument values that survive a suspension — formally identical to locals.
- a return slot, if the function returns non-`void`.

**Self-contained forbids frame-internal pointers.** Storing `&local` of a lifted local makes
the frame non-relocatable and breaks snapshot and reload. The transform detects this and
rejects it with a hard build error citing file:line. Pointers to memory outside the frame
are permitted and are the caller's serialization concern.

Generated ABI:

```c
typedef struct {
    i32  state;
    i32  i;
    i64  acc;
    i64  __ret;
} Mel_Cont_Frame_sum_to;

typedef enum {
    MEL_CONT_PENDING,
    MEL_CONT_DONE,
} Mel_Cont_Status;

Mel_Cont_Status sum_to__resume(Mel_Cont_Frame_sum_to* f, Mel_Cont_Yield* out);
```

The frame `struct` is emitted into the user's own header via the `mel_cont` macro, so it is
LSP-visible and `sizeof` is usable at call sites; the codegen emits only the implementation
`.c`. No generated header.

## User surface

```c
mel_cont(sum_to, (i64 n), i64) {
    i64 acc = 0;
    for (i32 i = 0; i < n; i++) {
        acc += i;
        mel_cont_yield(acc);
    }
    mel_cont_return(acc);
}
```

- `mel_cont(name, params, ret[, yield])` — marks the function via
  `__attribute__((annotate("mel:continuation")))`. An optional fourth argument types the
  yield channel; absent it, yields carry no value. On non-clang toolchains the macro degrades
  to a forward declaration so pre-generated or hand-written implementations still link.
- `mel_cont_yield(v)` / `mel_cont_yield()` — suspension point; surfaces `v` through the typed
  `Mel_Cont_Yield` channel, or suspends purely.
- `mel_cont_await(child)` — suspension point that drives a child continuation to completion,
  re-suspending the parent on each child suspension. This composes continuations without a
  scheduler and is itself expanded by the transform.
- `mel_cont_return(v)` / `mel_cont_return()` — completion; writes the return slot and sets the
  terminal state. Falling off the end is an implicit `mel_cont_return()`.

Driving a frame:

```c
Mel_Cont_Frame_sum_to f = {0};
Mel_Cont_Yield y;
while (sum_to__resume(&f, &y) == MEL_CONT_PENDING) {
    use(y);
}
i64 total = f.__ret;
```

There is no `create`/`destroy`. Birth is zero-init; death is the caller dropping the storage.
That absence is the no-alloc essence made visible.

**Multi-shot.** Because the frame is self-contained, a frame may be copied and both copies
resumed independently. This is the substrate for rollback fan-out and costs nothing given the
self-containment rule.

## Composition with the engine

A continuation is a leaf primitive. It integrates by being driven, never by pulling in the
runtime.

- **Reactor / async.** A fiber-side task awaits a continuation by resuming it in a loop,
  yielding the fiber whenever the continuation surfaces a "waiting on X" yield. The bridge
  lives entirely on the fiber side; the continuation core carries no reactor dependency.
- **Audio realtime.** The realtime callback resumes continuations directly — no bridge, no
  queue on the resume path. Values the control thread must observe cross back through the
  existing lock-free SPSC queues, carrying yielded values, never frames.
- **Massive count.** Ten thousand frames in a pool array, one `resume` sweep per tick. Each
  frame is kilobytes or less and contiguous — the cache packing per-fiber stacks cannot match
  (MEL-ENGINE-VI).
- **Serialization.** Turning a frame into bytes belongs to the owning save/netcode module.
  This module owes only the flat, internally-pointer-free layout and a per-continuation layout
  hash so the consumer can refuse or migrate a mismatched frame.

## The transform

### Substrate constraint

The libclang C API is read-only: it yields an AST with token-accurate source ranges but no
rewriter and no faithful C pretty-printer. The transform therefore cannot re-emit arbitrary C
by pretty-printing the AST. It:

1. parses the marked function for the AST and the exact source byte-ranges of every leaf
   construct;
2. lowers the body to a control-flow IR — basic blocks, edges, and a distinguished suspend
   edge at each `mel_cont_yield` / `mel_cont_await`;
3. runs liveness over the IR to fix the lift set;
4. emits fresh C from the IR, splicing original source byte-ranges verbatim for leaf
   expressions and non-suspending statements, and generating only the dispatch scaffold — the
   `switch`, the state assignments, the lifted-local rewrites.

Splicing leaf ranges and authoring only the scaffold is the keystone that makes a C-API-only
source-to-source transform tractable: the user's expressions, macros, and types are copied
byte-for-byte; only the control skeleton is synthesized.

### State-machine lowering

Each suspension point splits its basic block:

```c
switch (f->state) {
case 0:
    f->state = 1; return MEL_CONT_PENDING;
case 1:
    ;
}
```

Control flow crossing a suspension, by difficulty:

- straight-line — split and renumber.
- `if`/`else` with a yield in a branch — the join after the `if` becomes a new state both
  branches route to.
- `while`/`for`/`do` with a yield in the body — the back-edge resets `state` to the loop-
  header case; the induction local is lifted, being live across the back-edge. This is the
  case hand-rolled protothread macros cannot do without manual lifting, and the reason codegen
  exists.

### Liveness and the lift set

A local is lifted iff it is live across at least one suspend edge — assigned before some
suspend edge and read after it on a path through that edge (standard backward liveness over
the IR). Everything else stays a true C local, correct because it is provably dead across
every suspension. Lifting only the live set, not all locals, keeps frames minimal.

### Supported and rejected constructs

Supported: straight-line code, `if`/`else`, `while`, `for`, `do`/`while`, nested combinations,
`mel_cont_yield`, `mel_cont_await`, `mel_cont_return`, automatic lifting including loop
induction variables.

Rejected, as a hard build error citing file:line — never silent degradation
(MEL-ENGINE-VIII): a `switch` crossing a suspension; `goto` or a label crossing a suspension;
taking the address of a lifted local; a VLA as a lifted local; a suspension point inside a
macro argument whose range cannot be spliced cleanly.

### Teardown

A continuation has no destructors. Teardown is the caller ceasing to resume and dropping the
frame. A lifted local may not own an external resource; resources live with the driver, so a
dropped mid-flight frame leaks nothing. Dropping a mid-flight frame is defined behavior, not a
surprise.

## Build integration

The build follows the existing enum-str codegen tool.

- **Tool** `tools/codegen/continuation_gen.c` — libclang C API, same discovery and caching path
  as `enum_str_gen.c`. Parses an umbrella TU, finds `mel:continuation`-annotated
  `FunctionDecl`s, runs the transform, emits `continuation.generated.c`.
- **Runner** `run_continuation_codegen(ctx)` in `tools/build/runner_codegen.c`, hooked into
  `emit_target_edges` after source resolution and before the cc edges; mtime-gated against the
  tool and the marked sources; appends the generated `.c` to `ctx->sources`. The TU is listed
  in `nob.c`'s `NOB_GO_REBUILD_URSELF_PLUS` so a driver edit triggers a rebuild.
- **Marker macros** in `modules/reflect/include/reflect/continuation.h`: `mel_cont`,
  `mel_cont_yield`, `mel_cont_await`, `mel_cont_return`, with their annotate-attribute
  expansions and non-clang fallbacks.
- **Build API** `mel_build_generate_continuation(t, "path/to/marked.c", ...)`, backed by a
  `File_Paths continuation_sources` field on the target.
- **Module** `modules/continuation/`, top-level, namespace `<continuation/...>`. Not under
  `async/`: a continuation is the non-async, non-fiber primitive, and filing it under async
  repeats a category error.
- **Host dependency:** a full LLVM install on the build host, via `$MEL_LIBCLANG_PREFIX` or
  the default prefix, matching the enum-str tool. Failure is loud; the non-clang fallback keeps
  declarations intact.

## Failure modes and their mitigations

- **Re-emission infidelity** corrupting macros or types — eliminated by splicing leaf source
  ranges rather than re-printing user C.
- **Liveness miscompute** reading a dead or garbage local, the worst outcome — conservative
  bias (lift when unsure); a verification mode asserting every read of a non-lifted local is
  dominated by an assignment with no intervening suspend edge; differential testing.
- **Frame size drift** breaking embedders — ordinary recompilation, not ABI breakage, except
  for serialized frames, handled by the layout hash.
- **Self-referential pointer** breaking relocation — detected and rejected.
- **Suspension in an unsupported construct** — hard rejection with file:line.
- **Serialized frame against a changed layout** — a per-continuation layout hash surfaced to
  the consumer, which refuses or migrates; the module never silently loads a mismatched frame.
- **Reactor bridge allocating or blocking on the audio path** — the bridge is fiber-side only;
  the continuation core has no reactor dependency, enforced by module boundaries.
- **`mel_cont_await` resume cost** — a depth-N await chain resumes through N parent dispatchers
  per step, O(depth) per resume. A correctness-neutral cost, documented; a flattening pass is
  possible if it bites.
- **libclang absent or mismatched** — loud failure with the `$MEL_LIBCLANG_PREFIX` escape; the
  non-clang fallback preserves declarations.
- **Android edge path bypassing codegen** — wired into `emit_android_edges` when an Android
  target opts in.

## Testing

The transform is a compiler; it is tested like one.

- **Differential execution.** For each fixture, a hand-written straight-line equivalent; the
  continuation driven to completion must yield the identical sequence and return value. Catches
  liveness miscompute behaviorally.
- **Golden frame layout.** A snapshot of the generated frame struct and layout hash per fixture,
  diffed on change so layout drift is never silent.
- **Rejection fixtures.** Each rejected construct asserts a hard build error at the right
  file:line.
- **Snapshot round-trip.** `memcpy` a mid-flight frame, resume the copy, assert identical
  continuation — proving self-containment.

These run under the synthesized `melody-test` target.

## Components

The build decomposes into units related by dependency, not sequence:

- **`continuation/abi`** depends on nothing — the frame struct shape, `Mel_Cont_Status`,
  `Mel_Cont_Yield`, the `resume` and return ABI, and a hand-written reference continuation with
  no codegen, which doubles as the differential-test oracle. Everything else targets this ABI.
- **`continuation/macros`** depends on the fixed ABI — `reflect/continuation.h`, the `mel_cont*`
  surface, annotate attributes, non-clang fallback.
- **`continuation/transform-straightline`** depends on the ABI and macros — the libclang tool
  for straight-line and `if`, with lifting, emitting output matching the reference.
- **`continuation/transform-loops`** depends on the straight-line transform — `while`/`for`/`do`
  and induction lifting.
- **`continuation/build-wiring`** depends on the tool — runner, build API, `nob.c` dep-list
  entry.
- **`continuation/compose`** depends on the ABI — `mel_cont_await`, the fiber-side reactor
  bridge, the audio resume path.
- **`continuation/serialize`** depends on the ABI — the layout hash and snapshot guarantees.
