# CLAUDE.md proposals — directing agents in a many-module repo

Problem: the repo has ~70 modules but no machine-legible statement of what each affords. Every
agent session opens with a survey expedition (reading include/ trees module by module), burns
context on rediscovery, and risks working against a module's intent. These proposals come from a
multi-agent migration session: each addresses a failure or cost actually incurred.

## 1. Module index — one line per module, affordance-first

Create `docs/modules.md`: one line per module, stating what it GIVES (not what it is), its key
types, and its posture. Reference it from CLAUDE.md so it loads into every session.

    - allocator — Mel_Alloc callback waist + arena/stack/ring/block/buddy/guard/track impls; everything takes `const Mel_Alloc*`
    - executor — the async waist: intrusive Mel_Task (zero-alloc, armed-coalesced), Mel_Executor vtbl, Mel_Waker
    - vat — one thread as control domain: sources (wakeables/deadline/drain/cancel), waiters (kqueue/cocoa/guest), drivers, ticks, timers, vsync; replaces reactor
    - future — one-shot write-once future; consume ONLY via mel_future_then on an executor
    - boot — framework-owned main; apps define mel_app_setup(Mel_Vat*) and never block
    ...

Rule of authorship: the line answers "an agent needs X — which module?" in one read. Curated by
hand; each module's `readme.md` opening sentence is the canonical source the index restates.

## 2. Readme discipline — a fixed `Posture` section

Module readmes exist but unevenly (several modules ship none), and none state the integration
facts an agent needs instantly. Mandate four sections in every `modules/<m>/readme.md`:

- **Why** — one paragraph, what gap it fills.
- **Surface** — headers and the half-dozen load-bearing signatures.
- **Posture** — the agent-critical facts: loop binding (takes a `Mel_Vat*` / executor-only /
  loop-free), allocator policy (per-call, per-instance, module-global), thread affinity
  (owner-asserted? cross-thread-safe entry points?), init style (open/close pair, refcount,
  framework-owned).
- **Owed** — confessed gaps (MEL-ENGINE-VIII), so agents extend the list instead of
  rediscovering or silently patching.

## 3. Pattern registry — named exemplars, not prose

Agents perform best when told "copy this file's shape", worst when told "design something
appropriate". Create `docs/patterns.md` mapping pattern names to canonical files:

    vat source (fd readiness)      modules/port/src/posix/port_backend.c
    vat source (deadline-paced)    modules/vat/src/tick.c
    in-flight op retention         modules/fs/src/fs.c
    executor-only module decouple  modules/camera (init takes Mel_Executor*)
    host-callback decouple         modules/window (Mel_Window_Host)
    loop-test harness              modules/port/test/test_port.c (run-helper)
    boot-hosted app                apps/hello-window
    future-chain state machine     apps/barcode-reader (stage = task run pointer)

CLAUDE.md addition: "When implementing against a module, find the pattern in docs/patterns.md
and copy its shape; deviating from a named pattern requires stating why in the writeup."

## 4. Concurrent-agent etiquette

Incurred failure: one agent changed a public init signature mid-session; a concurrent agent fed
the old pointer type through it and trapped at runtime. Additions:

- Before changing any public module signature, grep all consumers; consumers owned by a
  concurrent agent are listed in the report as pending edits, never edited.
- Every spawned agent receives an explicit do-not-touch list; the spawner maintains it.
- A live session board (`design/in-flight.md` or the active ledger) names each agent's
  territory; agents read it before claiming files.

## 5. Migration ledgers as the hand-off instrument

What worked: a ledger in the design spec (module → disposition → status) that agents read on
entry and flip on completion. Generalize: any multi-wave effort keeps a ledger in its design/
spec; CLAUDE.md instructs agents to read the relevant ledger before working and to flip exactly
the entries they completed — the ledger, not chat history, is the coordination medium
(MEL-SPEC-004 applied to agents).

## 6. Verification recipes — tribal knowledge made durable

Costs incurred rediscovering: the android emulator needs a manual cold boot
(`-no-snapshot-load`) before `nob run`; vulkan tests want `DYLD_LIBRARY_PATH=/opt/homebrew/lib
MEL_TEST_NOFORK=1`; GUI smoke testing is "run 3s in background, assert alive, kill"; audio apps
must not be run in unattended sessions. Collect these in `docs/verification.md`, one entry per
target/platform quirk, appended by whoever pays the discovery cost. CLAUDE.md points to it
beside the build commands.

## 7. Keep the mechanical laws separable

Every agent prompt this session restated the five mechanical laws (no comments, no enums, no
fixed arrays, explicit allocators, clang-format). Restructure CLAUDE.md so they live under one
short heading quotable by reference ("obey the Mechanical Laws section of CLAUDE.md") instead of
being paraphrased per prompt — paraphrase drifts, and drift is how a law gets dropped.

## Suggested CLAUDE.md insertion (once 1–3 exist)

    ## Module map
    Before reaching for or modifying a module, read its line in @docs/modules.md, then its
    readme's Posture section. When implementing against a module, copy the named exemplar in
    docs/patterns.md. Active migrations keep ledgers in design/ — read yours before working,
    flip what you complete.
