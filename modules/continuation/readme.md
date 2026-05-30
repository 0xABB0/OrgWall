# continuation

A continuation is a **stackless coroutine whose suspended state is a reified delimited
continuation**: a flat, owned data value holding a resume index and every local that survives
a suspension. The runtime object *is* the delimited continuation; the codegen that mints it is
defunctionalization of continuations over a CPS-explicit suspension set. Namespace `<continuation/...>`,
symbol prefix `mel_cont_`.

This module is **self-contained and deliberately outside the `nob` build framework** (it is
excluded on every platform in `modules/build.c`). It owns its codegen tool, its own build/test
driver, and is authored to the golden spear rather than fitted to the discovery rules. See
[decisions.md](decisions.md) for why, and for every deviation from [spec.md](spec.md).

## Why it exists

Everything is downstream of one property: a suspended continuation's entire state is plain owned
data. Three capabilities are corollaries of that single property (MEL-ENGINE-IX), not separate
features:

1. **Runs where fibers are forbidden.** `resume` is a plain call against caller-owned memory —
   legal inside the audio realtime callback, which permits no fibers, no reactor, no waiting, no
   allocation.
2. **Snapshots.** A flat frame `memcpy`s to disk (save), rewinds (rollback netcode), or replays
   deterministically. A machine stack cannot — it embeds return addresses and self-referential
   pointers.
3. **Survives hot-reload.** The frame holds no code pointers (state is an integer index), so a
   reloaded module resumes a frame its prior incarnation minted, given a stable layout.

A use needing none of these three wants a fiber, not a continuation. Suspension is permitted only
in the marked body, never through a helper call — this is the primitive that refuses to grow into
a general coroutine and re-import the function-coloring problem.

## Dependencies

- **Runtime:** `core` (`<core/types.h>` for `i32`/`i64`/…). Nothing else. The generated `.c` and
  the frame structs are plain C that compile anywhere.
- **Codegen host:** a full LLVM/libclang install, via `$MEL_LIBCLANG_PREFIX` or the default
  `/opt/homebrew/opt/llvm`, matching the `enum_str` tool. Failure is loud.

## ABI — the POD frame

The frame is **caller-allocated, fixed-size, self-contained**. The runtime never allocates a frame
(MEL-ENGINE-III). For a continuation `name` with parameters `P`, return type `R`, and an inferred
yield type `Y`, the codegen emits into `<name>.gen.h`:

```c
typedef struct Mel_Cont_Frame_name {
    i32 state;            // resume index; 0 = not started, -1 = done, >0 = suspension points
    /* each referenced parameter, in declaration order */
    /* each local live across at least one suspension point */
    R   __ret;            // present iff R != void
} Mel_Cont_Frame_name;

#define MEL_CONT_LAYOUT_HASH_name 0x....ull

Mel_Cont_Suspended name__resume(Mel_Cont_Frame_name* __f, Y* __f_out);   // __f_out present iff Y exists
```

- `state` is an **integer index, never a code pointer** — this is what makes the frame relocatable
  and reload-safe. Sentinels live in `<continuation/abi.h>`: `MEL_CONT_STATE_START` (0),
  `MEL_CONT_STATE_DONE` (-1).
- `Mel_Cont_Suspended` (a `bool`, from `abi.h`) is `true` while the continuation is still suspended
  (resume again) and `false` once complete. There is no status enum (MEL-CODE-001).
- **Parameters live in the frame** and are initialized by the caller before the first resume; the
  resume function takes no argument channel of its own.
- **The yield value rides an out-parameter, never the frame.** It is live only at the instant of a
  suspension and is read immediately, so it stays off the frame, keeping frames minimal.
- A self-contained frame **forbids frame-internal pointers**: taking `&` of a lifted local is a hard
  build error. Pointers to memory *outside* the frame are permitted and are the caller's
  serialization concern.

The frame struct and prototype are **injected into the header where you wrote the continuation**,
inside a managed `/* >>> mel_cont generated frames >>> */` region, so `sizeof` and `{0}` init work
at the call site and the layout is LSP-visible. There is no separate generated header to include —
callers `#include` the header they already own. See decisions.md, decision 4.

## User surface

Write an ordinary-looking C function in a header (`*.cont.h`) and mark it. The four macros are in
`<continuation/cont.h>`. Codegen injects the frame struct + prototype into this same header; anyone
who wants to drive the continuation just includes it.

```c
// sum_to.cont.h
#pragma once
#include <continuation/cont.h>
#include <core/types.h>

/* >>> mel_cont generated frames >>> */
/*   (codegen injects Mel_Cont_Frame_sum_to + sum_to__resume here) */
/* <<< mel_cont generated frames <<< */

mel_cont(sum_to, (i64 n), i64)
{
    i64 acc = 0;
    for (i32 i = 0; i < n; i++)
    {
        acc += i;
        mel_cont_yield(acc);
    }
    mel_cont_return(acc);
}
```

You write only the `mel_cont(...) { … }` body and (after the first build) keep the committed file as
is — codegen owns the marked region. In editor/normal compiles the body lowers to a discarded
`static inline`, so including the header anywhere costs nothing; under codegen it is the analysed
source. The resume *definition* is emitted to a sibling `*.gen.c` that the build compiles and links
(callers never include it). See `example/` for a complete, runnable app (`./build example`).

- `mel_cont(name, params, ret)` — marks the function. `params` carries its own parentheses;
  `ret` is required (use `void`).
- `mel_cont_yield(v)` / `mel_cont_yield()` — a suspension point; surfaces `v` through the typed
  out-parameter, or suspends purely. The yield type is **inferred** from the yielded expressions
  (their canonical type) and must be consistent across all yields.
- `mel_cont_await(child)` — a suspension point that drives a child continuation frame to completion,
  re-suspending the parent on each child suspension, forwarding the child's yields when the channel
  types match. Composes continuations without a scheduler. See *Composition*.
- `mel_cont_return(v)` / `mel_cont_return()` — completion; writes `__ret` and sets the terminal
  state. Falling off the end is an implicit `mel_cont_return()`.

### Driving a frame

```c
Mel_Cont_Frame_sum_to f = {0};
f.n                     = 10;            // parameters are frame fields
i64 y;
while (sum_to__resume(&f, &y)) use(y);   // true while suspended
i64 total = f.__ret;
```

There is no `create`/`destroy`. Birth is zero-init; death is the caller dropping the storage. A
dropped mid-flight frame leaks nothing: a lifted local may not own an external resource (resources
live with the driver). **Multi-shot** is free — `memcpy` a frame and resume both copies
independently; this is the substrate for rollback fan-out.

### Reserved identifiers

A marked body must not use: `__f`, `__f_out`, `__ret`, any `__await_out_*`, or any identifier
beginning `__mel_cont`.

## How the transform works

libclang's C API is read-only: an AST with token-accurate source ranges, no rewriter, no faithful C
pretty-printer. So the transform never re-prints user C. It:

1. parses the marked header (with `-DMEL_CONT_CODEGEN`), under which the macros expand to
   clang-detectable, error-free sentinel calls and an `annotate("mel:continuation")` attribute;
2. walks the structured AST, recording every local/parameter, every reference (by **spelling**
   location, so macro-argument identifiers resolve to their real source bytes), every suspension
   point (whose call/argument bytes are located by paren-scanning the source), and every loop;
3. computes the **lift set** by liveness;
4. emits the resume function as a **Duff's-device state machine** by *splicing the original source
   byte-ranges verbatim* and authoring only the dispatch scaffold — the `switch`, the state
   assignments, the lifted-local rewrites. The user's expressions, macros, and types are copied
   byte-for-byte.

Splicing leaf ranges and synthesizing only the skeleton is the keystone that makes a C-API-only
source-to-source transform tractable.

### The state machine

Each suspension splits its block. Because the case labels sit *inside* the user's preserved control
flow (Duff's device), loops fall out for free: a `case` label inside a `for` body, jumped to from
the top-level `switch`, re-enters the loop mid-iteration; the loop's own condition and increment
resume normally. This is precisely what hand-rolled protothread macros cannot do without manual
lifting, and the reason codegen exists. See any `test/golden/*.gen.c` for the exact emitted shape.

### Liveness and the lift set

A local is lifted iff it is live across at least one suspend edge. The implementation is a
**conservative, kill-free** approximation: a local is lifted if some reference reaches it after a
suspension on any path — including the loop back-edge (any reference anywhere inside a loop that
encloses a suspension). Over-lifting only grows the frame; it is never incorrect, and conservative
bias is the sanctioned mitigation for the worst failure mode (reading a dead local). Parameters are
lifted iff referenced. The `await` child frame is always lifted (it survives the await's internal
re-suspensions).

### Supported and rejected

**Supported:** straight-line code, `if`/`else`, `while`, `for`, `do`/`while`, arbitrary nesting,
`mel_cont_yield`, `mel_cont_await`, `mel_cont_return`, automatic lifting including loop induction
variables, scalar and aggregate (`= {…}`) initializers on lifted locals.

**Rejected as a hard `file:line` build error** — never silent degradation (MEL-ENGINE-VIII):
a `switch` crossing a suspension; a `goto`/label in a body with any suspension; taking the address
of a lifted local; a VLA lifted local; a lifted *array with an initializer*; a suspension argument
whose source range cannot be spliced (came through another macro); any rewrite that would overlap.

## Composition with the engine

A continuation is a leaf primitive. It integrates by being driven, never by pulling in the runtime.

- **`mel_cont_await`.** Drives a child frame embedded in the parent frame. When the parent's and
  child's yield channels share a canonical type, the child's yields are forwarded through the
  parent's `__f_out`; otherwise the child's yields land in a per-await discard slot in the parent
  frame. A pure-relay parent (no yields of its own) adopts its child's yield type. Cost is O(depth)
  per resume through nested awaits — correctness-neutral, documented; a flattening pass is possible
  if it bites. The two continuations must be processed in the same codegen invocation so the parent
  knows the child's signature.
- **Reactor / async.** A fiber-side task awaits a continuation by resuming it in a loop and yielding
  the fiber on a "waiting on X" yield. The bridge lives entirely on the fiber side; this core
  carries no reactor dependency.
- **Audio realtime.** The realtime callback resumes continuations directly — no bridge, no queue on
  the resume path. Yielded values cross back through the existing lock-free SPSC queues.
- **Serialization.** Turning a frame into bytes belongs to the owning save/netcode module. This
  module owes only the flat, pointer-free layout and the per-continuation `MEL_CONT_LAYOUT_HASH_*`,
  so a consumer can refuse or migrate a mismatched frame.

## Build & test

Self-contained `nob`-style driver. Run from this directory:

```sh
cc -o build build.c        # bootstrap the driver (rebuilds itself on edit)
./build            # alias for `test`
./build tool       # just build the codegen tool (.cache/continuation_gen)
./build gen        # run codegen on every fixture, golden-check the output
./build bless      # regenerate and overwrite the committed goldens
./build test       # gen + golden + differential + snapshot + rejection suite
```

The codegen tool's contract:

```
continuation_gen <header.cont.h> <out.gen.c> [clang-args...]
```

It reads the marked header, **injects the frame structs + prototypes into that header in place**
(idempotently, inside the managed region), iterating until the region stabilizes — which resolves
the `await` bootstrap, since a parent that names a sibling's frame type now sees it in the same
header. It then emits the resume definitions to `<out.gen.c>` (which `#include`s the header).
`build.c` runs the tool on scratch copies of the fixtures so the committed fixtures stay pristine;
the `example` target runs it in place so you can see the injected header as a real artifact.

### The test suite (tested like a compiler)

- **Differential execution** (`test/driver/*_diff.c`): a hand-written straight-line oracle; the
  driven continuation must yield the identical sequence and return value. Catches liveness
  miscompute behaviorally. Fixtures cover `for` + induction lifting (`sum_to`), pure-yield `while`
  (`countdown`), `if`/`else` (`classify`), `do`/`while` (`repeat_sum`), and `await` with forwarding
  (`relay`).
- **Golden frame layout** (`test/golden/`): the injected `.cont.h` (frame region in context) and the
  `.gen.c` per fixture, diffed on every run so layout drift is never silent. Goldens are
  intentionally unformatted — deterministic and formatter-independent.
- **Snapshot round-trip** (`test/driver/snapshot.c`): `memcpy` a mid-flight frame, resume the copy,
  assert identical continuation — proving self-containment.
- **Rejection fixtures** (`test/reject/`): each rejected construct asserts a hard build error at the
  right `file:line`.

## Layout

```
include/continuation/abi.h   ABI: state sentinels, Mel_Cont_Suspended, Mel_Cont_Layout_Hash
include/continuation/cont.h  the mel_cont* macro surface (codegen + editor modes)
codegen/continuation_gen.c   the libclang source-to-source transform (the compiler)
build.c                      self-contained nob driver: build tool, codegen, golden, run tests
example/ticker.cont.h        a runnable continuation (the injected region is committed)
example/app.c                drives it — the canonical "how you use this"
test/fixtures/*.cont.h       marked continuations (pristine; codegen runs on scratch copies)
test/driver/*.c              differential oracles + drivers + snapshot, and harness.h
test/golden/*.cont.h,*.gen.c committed golden output (injected header + resume impl)
test/reject/*.cont.c         fixtures that must be rejected with file:line
spec.md                      the originating design (source of truth for intent)
decisions.md                 every decision taken, and every deviation from spec.md
todo.md                      known gaps and future work
```

The single-TU tool mirrors `tools/codegen/enum_str_gen.c`; its internal sections map to the spec's
component decomposition (abi, macros, transform-straightline, transform-loops, compose, serialize).
