# GPU RHI M2 — agent-team push + integration

A four-track agent team advanced `design/gpu-rhi.md` (mid-M2, Vulkan/MoltenVK runnable half)
in parallel worktrees off `2854d0b`; this orchestration session integrated all four into `main`
with verification after each merge. The per-track detail lives in the four sibling writeups dated
2026-06-02 (`stress-audit`, `visual-tests`, `hello-gpu-showcase`, `m2-builder`); this file is the
integrated picture and the consolidated follow-up ledger.

## Tracks merged (in order, each `--no-ff`, verified post-merge)
- **auditor** (`12d649a`) — `gpu-stress` suite (14 probes) + a severity-ranked defect report.
  Read-only on `src/`; surfaced 2 Critical / 3 Major / spec-deviations.
- **tester** (`beb6b87`) — `gpu-visual` golden harness (offscreen → readback → pixel-assert + PPM
  dump): uniform-buffer bindless (the previously-unproven heap class), bindless sampled texture,
  alpha src-over, multi-pass UBO. The lone `build.c` conflict (two test-target blocks at one anchor)
  was a keep-both.
- **appsmith** (`a5c6585`) — 7 hello-gpu showcase screens (texquad/plasma/depth/layers/post/
  instances/gallery), host menu + `HELLO_GPU_AUTO` wired; enabled `descriptor_indexing` +
  `buffer_device_address` on the host device.
- **builder** (`6eb10a3`) — 5 backlog slices (shared `build_pipeline_layout` helper, storage-image
  bindless, `cmd_dispatch_indirect`, `synchronization2` lowering, MSAA resolve) **and** the 5
  stress-audit defect fixes.

## Audit defects — found by auditor, fixed by builder
- **CRITICAL-1** over-cap bindless registration: was debug-assert / release-silent-drop (slot ==
  index vs fixed class cap). Now pre-flight per class, fail loudly via new
  `..._CREATE_BINDLESS_SLOT_EXHAUSTED` status + rollback; guarded by
  `vk_bindless.sampler_over_cap_fails_loudly`. (Loud-status fix, **not** grow-on-demand — see below.)
- **CRITICAL-2** `cmd_begin_rendering` fixed `[8]` color-attachment truncation → dynamic.
- **MAJOR-3** buddy live-bytes now account the rounded block size (VRAM was under-reported ~2×).
- **MAJOR-4** staging uploads reserve a serial + route the staging free through the deferred-free
  watermark (was a latent UAF once uploads go async).
- **MAJOR-5** `queue_submit` guards null/empty `command_lists`.

## Verification (integrated `main`, Apple M3 Pro / MoltenVK 1.2.334, validation on)
- gpu-foundation 8/8 · gpu-vulkan **32/32** (was 28) · gpu-stress 14/14 · gpu-visual 4/4 — **58 tests**,
  zero VUID / leak / unexpected validation error (only the intentional negative-test diagnostics
  remain: MissingFeature / MissingBindlessSlot / BindlessSlotExhausted).
- hello-gpu: clean build, all 10 screens headless ~3.5 s each, 0 problem-lines, swapchain up.
- `barrier lowering: synchronization2` and `render lowering: dynamic rendering` both active.

## Public API added this session (all additive; existing call sites unchanged)
- `MEL_GPU_BUFFER_INDIRECT` (buffer.h); `mel_gpu_cmd_dispatch_indirect` (command.h);
  `Mel_Gpu_Color_Attachment.resolve_view` (rendering.h);
  `MEL_GPU_{TEXTURE_VIEW,BUFFER,SAMPLER}_CREATE_BINDLESS_SLOT_EXHAUSTED` — a new failure mode a
  bindless-device caller should branch on (previously a crash / silent corruption).

## Kludges carried (confessed, MEL-ENGINE-VIII) — consolidated
- CRITICAL-1 fix is loud-status, not grow-on-demand; fixed heap-class caps remain (MEL-CODE-002
  tension). Grow-on-demand is the eventual right answer.
- sync2 / MSAA / dispatch-indirect each lower a subset, not the full §7.1/§7.2/§7.3 surface.
- MAJOR-4 staging path is still `vkQueueWaitIdle`-synchronous (watermark routing in place for when
  it goes async).
- appsmith offscreen targets fixed at 1024×768 blit-stretched (no public swapchain-extent accessor);
  per-frame `first_frame` bookkeeping works around the swapchain frame recorder's U17 state-tracker
  not being reset at `frame_begin`.
- MoltenVK logs "1 MB still allocated" at VkPhysicalDevice destroy — pre-existing driver artifact,
  not an engine leak (confirmed against untouched-baseline logs).

## Open follow-ups
- Engine: reset (or document) the swapchain frame recorder's per-frame state-tracker; add
  `mel_gpu_swapchain_extent(sc)`; grow-on-demand bindless heap classes.
- Coverage gaps (auditor): no multi-threaded U36 concurrency probe (headless synchronous single-queue
  device); per-queue retirement watermark must land *with* the async-queue slice.

## Needs Gabbo
- **Slang/codegen go-ahead** — the `melody.binding` mixin (M2 deliverable A3) rides the undocumented
  codegen path (CLAUDE.md halt-and-query). Untouched; index/BDA duality still shown via hand-authored
  shaders.
- **Comments vs Rule-#1** — global `~/CLAUDE.md` says "Never write comments"; `modules/gpu` and the
  apps are densely commented house-style, and every track matched it. All four agents flag the tension
  and will strip on your word.
- Nothing pushed: `main` is ahead of `origin/main`; `origin` (and win-pilot for D3D12) await your say.
