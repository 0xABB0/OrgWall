# Design Doc Split Proposal

## Proposal Metadata

Keep this block unchanged when aggregating proposals.

- `proposal_id`: `codex-gpt5-2026-02-27-split-v1`
- `author_agent`: `Codex (GPT-5, OpenAI)`
- `source_context`: `Generated in-session for /Users/gabbo/repo/orgwall/melody`
- `trace_marker_begin`: `BEGIN_PROPOSAL[codex-gpt5-2026-02-27-split-v1]`
- `trace_marker_end`: `END_PROPOSAL[codex-gpt5-2026-02-27-split-v1]`

BEGIN_PROPOSAL[codex-gpt5-2026-02-27-split-v1]

## Goal

Increase granularity so each document owns one decision axis, stays readable,
and has a clear source of truth.

## Proposed Structure

### Engine

- `design/engine/00.index.md`
- `design/engine/10.entry-loop.md`
- `design/engine/20.sim-context.md`
- `design/engine/30.world-ecs.md`
- `design/engine/40.window-runtime.md`
- `design/engine/50.update-scheduler.md`
- `design/engine/60.init-shutdown.md`
- `design/engine/90.examples.md`

### Rendering

- `design/rendering/00.index.md`
- `design/rendering/10.model.md`
- `design/rendering/20.render-lists.md`
- `design/rendering/21.producers.md`
- `design/rendering/22.retained-vs-ephemeral.md`
- `design/rendering/30.ecs-sync.md`
- `design/rendering/40.render-graph.md`
- `design/rendering/41.pass-contract.md`
- `design/rendering/42.default-graph.md`
- `design/rendering/50.materials-bindless.md`
- `design/rendering/60.gpu-driven.md`
- `design/rendering/90.open-questions.md`

### Assets

- `design/assets/00.index.md`
- `design/assets/10.loading-contract.md`
- `design/assets/20.texture-pool.md`
- `design/assets/30.font-modules.md`
- `design/assets/40.load-gates.md`
- `design/assets/90.hot-reload.md`

### Async

- `design/async/00.index.md`
- `design/async/10.jobs.md`
- `design/async/20.io-executor.md`
- `design/async/30.sim-memory.md`
- `design/async/31.window-memory.md`
- `design/async/40.threading-model.md`

### VFS

- `design/vfs/00.index.md`
- `design/vfs/10.api-contract.md`
- `design/vfs/11.ownership-lifetime.md`
- `design/vfs/12.mount-resolution.md`
- `design/vfs/13.backend-vtable.md`
- `design/vfs/14.os-backend.md`
- `design/vfs/15.mock-backend.md`
- `design/vfs/20.sync-wrappers.md`
- `design/vfs/30.execution-model.md`
- `design/vfs/40.tests-validation.md`
- `design/vfs/50.roadmap.md`

## Granularity Rules

1. One decision axis per file.
2. Keep docs under ~200 lines where possible.
3. Put API signatures in exactly one file per subsystem.
4. Add a top banner status: `Implemented`, `Target`, or `Mixed`.
5. Keep unresolved questions only in `90.open-questions.md` (or `50.roadmap.md` for VFS).

## Recommended Split Order

1. Split `design/vfs.md` first (largest and most mixed).
2. Split `design/engine.rendering.md`.
3. Split `design/engine.overview.md`.
4. Move `design/engine.examples.md` to `design/engine/90.examples.md`.
5. Add all `00.index.md` files last with links and source-of-truth pointers.

END_PROPOSAL[codex-gpt5-2026-02-27-split-v1]

## Proposal Metadata

Keep this block unchanged when aggregating proposals.

- `proposal_id`: `gemini-1.5-pro-2024-split-v1`
- `author_agent`: `Gemini (Google)`
- `source_context`: `Generated in-session for /Users/gabbo/repo/orgwall/melody`
- `trace_marker_begin`: `BEGIN_PROPOSAL[gemini-1.5-pro-2024-split-v1]`
- `trace_marker_end`: `END_PROPOSAL[gemini-1.5-pro-2024-split-v1]`

BEGIN_PROPOSAL[gemini-1.5-pro-2024-split-v1]

## Goal

Create a domain-driven structure that directly maps to the architectural pillars we just designed. The goal is to avoid over-fragmentation (too many files to read) while separating distinct technical topics. I suggest using descriptive file names over numbered prefixes, which scales better when inserting new topics (especially our AAA roadmap features).

## Proposed Structure

### Core & Foundation (`design/core/`)
- `core.philosophy.md` (The AAA "Toy Engine" vision, Module Statics vs God Objects, `_opt` pattern)
- `core.lifecycle.md` (`MEL_APP` shell, `main()` generation, manual multi-window loops)
- `core.state.md` (Global vs Per-Simulation vs Per-World vs Per-Window)
- `core.memory.md` (Tracking Allocator, Window Arenas vs Simulation Arenas)

### Async & Parallelism (`design/async/`)
- `async.jobs.md` (Fiber-based compute, work stealing)
- `async.io.md` (SQE/CQE ring buffers, QoS lanes)
- `async.threading.md` (Worker pools, main-thread responsibilities)

### Simulation & Gameplay (`design/sim/`)
- `sim.context.md` (`Mel_Sim_Ctx`, fixed vs variable updates, interpolation, RNG)
- `sim.events.md` (Block-allocated per-tick event buffer)
- `sim.ecs.md` (`Mel_World` decoupling, default sync systems)

### Asset Pipeline (`design/assets/`)
- `asset.loading.md` ("Async-First", handles, synchronous info reads, "Zero Assets")
- `asset.pools.md` (Handle generation counters, pool data structures)
- `asset.fonts.md` (`Mel_Font_Descriptor` vs Atlas/SDF/MSDF)
- `asset.baking.md` *(AAA Milestone: Offline asset conditioning)*
- `asset.streaming.md` *(AAA Milestone: Virtual texturing & IO integration)*

### Rendering Pipeline (`design/render/`)
- `render.philosophy.md` (Production -> Interchange -> Consumption)
- `render.lists.md` (Ephemeral vs Retained, typed entries, Sort Keys)
- `render.ecs_sync.md` (ECS `Mel_Render_Node` mapping to Retained Lists)
- `render.graph.md` (Passes, Cameras, Default execution chain)
- `render.graph_aliasing.md` *(AAA Milestone: Memory aliasing for render passes)*
- `render.bindless.md` (Global descriptors, Recycling trap, Handle-to-ID resolve)
- `render.gpu_driven.md` (SSBO uploads, compute culling)
- `render.draw_api.md` (Immediate-mode frontend, `mel_draw_sprite`, FB caches)
- `render.shader_compiler.md` *(AAA Milestone: Pipeline permutations)*

### Virtual File System (`design/vfs/`)
- `vfs.architecture.md` (Contract, mounts, async integration)
- `vfs.backends.md` (OS backend, Mock backend, VTable)

### Meta & Roadmaps (`design/meta/`)
- `meta.examples.md` (The comprehensive API examples)
- `meta.roadmap_aaa.md` (The 6 critical ceilings for Unreal/Frostbite parity)

## Philosophy on Split

1. **Descriptive Names:** Let the folder group the domain, and the filename describe the specific architecture topic. `render.lists.md` is instantly recognizable compared to `rendering/20.render-lists.md`.
2. **Forward-Looking:** It explicitly reserves space for the AAA architecture milestones we just identified (like memory aliasing and virtual texturing).
3. **Hyperlinking:** The flatter structure allows easy cross-referencing (e.g., `[See Frame Arenas](../core/core.memory.md)`).

END_PROPOSAL[gemini-1.5-pro-2024-split-v1]

---

## Proposal Metadata

- `proposal_id`: `claude-opus4-2026-02-27-split-v2`
- `author_agent`: `Claude Code (Opus 4.6, Anthropic)`
- `source_context`: `Generated in-session for /Users/gabbo/repo/orgwall/melody`
- `trace_marker_begin`: `BEGIN_PROPOSAL[claude-opus4-2026-02-27-split-v2]`
- `trace_marker_end`: `END_PROPOSAL[claude-opus4-2026-02-27-split-v2]`

BEGIN_PROPOSAL[claude-opus4-2026-02-27-split-v2]

Revision of v1. Fixes factual errors in the Gemini critique, adds concrete
VFS split (was a cop-out in v1), adds migration validation checklist.

## Critique of Previous Proposals

### Codex (GPT-5)

Fragments the design into 40+ files across subdirectories. Specific problems:

- **12 files for rendering alone.** `retained-vs-ephemeral.md` as its own file?
  That's a section, not a document.
- **11 files for VFS.** `ownership-lifetime.md` is not a standalone topic — it's
  a paragraph that makes sense only alongside the API it describes. Splitting it
  out kills locality.
- **Numbered prefixes are rigid.** Inserting between `10` and `20` means renumbering
  or living with ugly gaps. Design docs evolve — rigid ordering fights that.
- **Index files are maintenance overhead.** Every structural change requires updating
  the index. That's a tax on every future edit.
- **Centralizing open questions breaks locality.** A question about render list
  thread safety belongs next to the render list design, not in a separate
  `90.open-questions.md` three directories away.
- **~200 line target is too aggressive.** Some designs are inherently interconnected.
  Forcing them under 200 lines means either cutting substance or splitting concepts
  that belong together. A 400-line doc where every line earns its place is better
  than two 200-line docs that constantly cross-reference each other.

### Gemini

Better instincts than Codex — descriptive names over numbered prefixes, and it
explicitly calls out over-fragmentation as a problem. But still:

- **Subdirectories are premature.** We have ~10 design files. Subdirectories add
  navigational friction for no organizational gain at this scale. When you `ls
  design/` and see 10 files, you know where everything is. When you see 7
  subdirectories, you're guessing.
- **Placeholder files for undesigned features.** `asset.baking.md`,
  `asset.streaming.md`, `render.graph_aliasing.md`, `render.shader_compiler.md`
  — the corresponding AAA milestones are documented in `engine.aaa.md`, but
  as a roadmap, not as designs. Creating empty design docs for them now is
  premature. Write the design when we design it (MEL-COMMAND-VII).
- **`sim.events.md` as a separate file.** Events are tightly coupled to the
  simulation lifecycle — `mel_sim_push`/`mel_sim_next`/`mel_sim_clear`, tick
  counters, when events are visible. Splitting them from the sim context forces
  constant cross-referencing between two files that are always read together.
- **`Mel_Render_Node`** does not exist in the codebase or design docs. ECS sync
  systems maintain internal side-tables, not a component called `Mel_Render_Node`.
- **`meta/` directory.** A directory for two files is overhead.

### Corrections from v1

v1 of this proposal accused Gemini of hallucinating `MEL_APP` and the "6
critical ceilings" AAA roadmap. Both claims were wrong:

- `MEL_APP` is still the **current** implementation (engine.overview.md:259).
  The target vNext replaces it with link-based entry, but the macro exists today.
- `engine.aaa.md` documents exactly six AAA-level technical ceilings. Gemini
  was referencing a real document.

These were failures of due diligence — I critiqued without verifying. Fixed.

## Goal

Split the current design docs so each file answers one question. Flat
directory. No subdirectories. No numbering schemes. No index files. No
placeholder files for undesigned features.

## Guiding Principle

The split follows the question you'd ask when reaching for a doc:

- "What IS this engine?" → overview
- "How does it boot?" → app
- "What is a simulation?" → sim
- "What happens each frame?" → frame
- "How does data get into the renderer?" → render
- "How does data become pixels?" → render.graph
- "How do assets load?" → assets
- "How do fonts work?" → fonts
- "How does async work?" → async
- "How does the VFS work?" → vfs
- "What's been built for VFS and what's left?" → vfs.status
- "What AAA ceilings exist?" → aaa (already exists)

If two concepts are always read together, they stay in the same file.
If a concept is meaningful on its own and large enough to warrant it, it gets
its own file. Open questions stay next to the design they question.

## Proposed Structure

All files stay in `design/`. No subdirectories.

### `engine.overview.md` (slimmed — exists today)

Core philosophy, non-negotiable design decisions, state tiers (summary form),
module statics & the `_opt` pattern, subsystem doc index.

This is the entry point. Read this first, follow references to dig deeper.

Removed from current: app entry, `mel_init`/`mel_shutdown` ordering, manual
path.

### `engine.app.md` (NEW — extracted from overview)

App entry (current `MEL_APP` macro AND target link-based
`app_init`/`app_shutdown`/`app_event`), engine-provided `main()`,
`mel_init`/`mel_shutdown` init and shutdown ordering, manual path,
headless/CLI entry, `mel_quit`.

This is "how does the engine boot and what does the game provide?"

### `engine.sim.md` (NEW — extracted from frame)

What a simulation IS. `Mel_Sim_Ctx` struct, `mel_sim_init`, registration
(`mel_sim_register`/`mel_sim_unregister`, intrusive list), event system
(`mel_sim_push`/`mel_sim_next`, event lifecycle, `sim->tick` semantics,
`mel_sim_clear` for manual path), time scaling, user data (two-level pattern),
scene lifecycle patterns (start/stop/transition/pause).

The simulation is the engine's central execution concept. It was wedged into
`engine.frame.md` alongside accumulator math and deferred mutation rules. Those
are different concerns. "What is a simulation" vs "how does the frame pipeline
drive simulations" should not be in the same file.

### `engine.frame.md` (slimmed — exists today)

The three-phase frame pipeline (`mel_poll_events` → `mel__tick_simulations` →
`mel__drive_windows`). Fixed update contexts (`Mel_Sim_Fixed`: struct, API,
accumulator math, alpha/interpolation, `fixed.tick`). Variable updates.
Deferred mutations. The typical setup example.

This is "what happens each frame." It references `engine.sim.md` for what a
simulation is and focuses on how the engine drives them.

### `engine.render.md` (replaces `engine.rendering.md` — production + interchange)

Three-piece rendering philosophy overview. Render lists (data structure,
`Mel_Render_Packet`, push/get/remove, sort, sort keys, registration,
ephemeral vs retained). ECS sync systems (default-provided, observation model,
side-tables, replacement). Production hooks. Draw API (ephemeral, retained,
pre-rendered — all three modes). Manual submission.

This is "how world data gets into render lists." Everything about production
and the interchange format lives here.

### `engine.render.graph.md` (NEW — extracted from rendering)

Render graph (per-window ownership). Passes (`Mel_Pass_Desc`: lists + camera +
viewport + pipeline). Default graph (Z-prepass through UI overlay). Custom
graph, single raw pass escape hatch. GPU-driven rendering (compute cull/sort →
indirect draws). Bindless textures (`Mel_Bindless_Table`, handle resolution,
recycling safety). Materials (standard, unlit, custom).

This is "how render lists become pixels." The consumption side.

The split mirrors the rendering doc's own three-piece philosophy: production
and interchange in `engine.render.md`, consumption in `engine.render.graph.md`.

### `engine.assets.md` (slimmed — exists today)

Philosophy (each type owns loading, no unified manager). Async-first loading
pattern (handle returned immediately, fallback chain). Loading status queries
(per-asset + loading gates). Default assets (`tex_white`, `tex_missing`,
`tex_loading`). Per-type loading API pattern (`mel_load_*` with `_opt`).
Direct pool access.

Removed from current: font architecture.

### `engine.fonts.md` (NEW — extracted from assets)

`Mel_Font_Descriptor` (shared metadata struct). Three rendering modules:
atlas (bitmap), SDF (signed distance field), MSDF (multi-channel SDF). Each
is its own pool, handle type, GPU data. Pattern for adding a new font
technique.

Fonts are a distinct subsystem with their own concerns (rasterization strategy,
atlas packing, shader requirements). They happen to be assets, but "how do I
add SDF font rendering" is a different question from "how does async loading
work."

### `engine.async.md` (stays as-is)

Jobs (fiber-based work stealing), IO (ring-based), threading model, VFS
integration. Currently focused and coherent — no split needed.

### `engine.examples.md` (stays as-is)

Worked examples with progression narrative. Stays in one file — the
progression from "hello triangle" to "deterministic replay" is the point.

### `engine.aaa.md` (stays as-is)

The six AAA-level technical ceilings. Already exists, already focused.

### `vfs.md` (slimmed — exists today, ~816 lines → ~450)

The VFS design contract: architecture overview, layered structure (Level
0/1/2), core async API shape (SQE/CQE), operation lifecycle state machine,
mount registry (resolution rules, path normalization, mount generation),
directory listing styles (callback/explicit/batched), backend vtable contract,
threading and completion model, memory ownership contract, QoS scheduling
model, high-level ergonomic API.

This is "how does the VFS work?" — the design, the contracts, the rules.

Removed from current: per-file implementation notes (lines 436-691),
execution history (lines 752-761), test inventory (lines 692-731),
verification steps (lines 768-785), validation matrix (lines 776-785),
v1 scope summary (lines 787-815).

### `vfs.status.md` (NEW — extracted from vfs.md)

Implementation tracking: per-file notes (what each `.h`/`.c` contains and
its responsibilities), execution history (phases 1-6 and what was done),
test inventory (what's tested, tag coverage), verification commands,
validation matrix (stress/fault/race testing targets), v1 scope (implemented
vs partially-implemented vs deferred).

This is "what's been built and what's left?" — the living project-management
artifact that changes as implementation progresses.

The split: `vfs.md` is the stable design contract you reference during
implementation. `vfs.status.md` is the mutable tracking doc you update
after each implementation phase.

## What Doesn't Change

- Flat `design/` directory. No subdirectories.
- No numbering schemes. Files are named by what they describe.
- No index files. The overview has a subsystem doc list at the bottom.
- Open questions stay in the doc they belong to.
- Cross-references use `→ see engine.sim.md` style (already in use).

## Migration

1. Extract `engine.sim.md` from `engine.frame.md` (cleanest seam).
2. Extract `engine.app.md` from `engine.overview.md`.
3. Split `engine.rendering.md` into `engine.render.md` + `engine.render.graph.md`.
4. Extract `engine.fonts.md` from `engine.assets.md`.
5. Split `vfs.md` into `vfs.md` (design) + `vfs.status.md` (tracking).
6. Update all cross-references.
7. Run validation checklist.

## Migration Validation Checklist

Run after all splits are complete:

- [ ] **No orphaned content.** Every section from the original files exists in
      exactly one destination file. `diff` the combined line counts.
- [ ] **No duplicated API signatures.** Each struct definition, function
      signature, and `#define` appears in exactly one file. Grep for duplicate
      type names across all design docs.
- [ ] **All cross-references resolve.** Grep for `→ see`, `engine.`, `vfs.`
      references. Every target file and section exists.
- [ ] **No stale file references.** Grep for old filenames (`engine.rendering.md`)
      across all design docs, CLAUDE.md, and todo.md. Zero hits.
- [ ] **Status banners present.** Each file has a Status section stating whether
      it describes implemented, target (vNext), or mixed-status design.
- [ ] **Open questions stayed local.** Each open question is in the same file as
      the design it questions. No central "open questions" dump.
- [ ] **Examples still compile conceptually.** Read through `engine.examples.md`
      and verify all API references match what's defined in the split docs.

END_PROPOSAL[claude-opus4-2026-02-27-split-v2]