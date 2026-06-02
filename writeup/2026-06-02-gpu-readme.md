# GPU module readme — scribe pass

## Work done
Wrote the long-absent `modules/gpu/readme.md` (flagged missing in six round-1 writeups), satisfying
the CLAUDE.md "Sources & modules" convention that each module folder carry a `readme.md`. Docs only;
no `src/`, `include/`, `build.c`, `test/`, or `apps/` touched — no build impact.

The readme covers: why the RHI exists (ceiling Vulkan Roadmap 2026 / D3D12 SM 6.9 / Metal 4, floors
degrade); backends + status (Vulkan runnable on macOS-MoltenVK + win32; D3D12 co-primary through the
Phase-3 binding model on the in-box Win10 floor; Metal/WebGPU future); deps from `build.c`; the
binding-model contract (heap at set 0, binding index = heap class, `slot == handle.index` for direct
families, the engine-owned `Mel_Gpu_Sampler_Indirect`, push-constant root-record carrier on the floor,
BDA pointer-vs-index payload duality, the classic `set_layouts`/bind-group peer, reflection-derived
vertex input, the `MISSING_FEATURE` vs `MISSING_BINDLESS_SLOT` split); build/test/run commands incl. the
macOS `DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1` requirement and the `HELLO_GPU_AUTO` hook;
and a layout note.

Folded the binding-model contract into a readme section rather than a separate `binding.md` — the
contract is tight and a second file would mostly duplicate the same header cross-references
(MEL-SPEC-001, no-duplication MEL-ENGINE-IX).

Every symbol, target, status enum, and command cited was verified in the tree (`ast-grep`/grep + Read
of the headers) before stating it.

## Inaccuracies found while cross-checking (the valuable part)
- **The task brief said "the indirect/`_Indirect` family for imports."** The realized public surface
  has exactly one indirect type, `Mel_Gpu_Sampler_Indirect`, and `sampler.h` states it is **engine-owned
  only** ("No foreign-sampler import exists — importing a sampler descriptor buys nothing — so the
  indirect family is engine-owned only"). The spec §3.1 *designs* the broader indirect-for-imports
  family (buffer / texture-view / accel-struct, import → `Borrowed`), but those peer types are not in
  the headers. The readme documents what is realized and notes the spec gap, rather than the brief's
  framing.
- **The brief hedged `gpu-concurrency` / `gpu-bench` "if present at write time."** Neither exists in
  `build.c`; only five test targets do (`gpu-foundation`, `gpu-vulkan`, `gpu-stress`, `gpu-visual`,
  `gpu-d3d12`). Not documented.
- **`HELLO_GPU_AUTO=triangle` is not a registered screen name.** The M1 writeups
  (`2026-06-01-gpu-rhi-m1.md`, `2026-06-02-gpu-rhi-m2-phase-a.md`) cite `HELLO_GPU_AUTO=triangle|cube`,
  but after the showcase merge `apps/hello-gpu/src/main.c` only string-matches `cube`, `lorenz`,
  `texquad`, `plasma`, `depth`, `layers`, `post`, `instances`, `gallery`; any unrecognized value falls
  through to the triangle. So `=triangle` works only as the default fall-through, not a named case.
  The readme lists the real names and notes the fall-through.
- **D3D12 reflection scope.** The `m2-d3d12-phase3` writeup is accurate but worth surfacing in one
  place: on D3D12 `reflect.c` reads the **input signature only**; `push_constant_size` and the
  `bindless` flag are explicit, not reflected (no in-box DXC reflection SDK); the classic path
  (`set_layouts`/bind groups) is unimplemented and rejected with `MISSING_FEATURE`;
  `mel_gpu_buffer_device_address` returns a real VA but the root-record payload stays
  `descriptor_indices`. The readme states each.

No contradictions found between the headers and the round-1 writeups beyond the `triangle` naming
drift above.

## Kludges (MEL-ENGINE-VIII)
None. Docs-only change; no shortcuts, no code.

## Rule-#1 tension (carried, every M2 GPU slice flags it)
Global `~/CLAUDE.md` says "Never write comments"; the project `CLAUDE.md` and the whole `modules/gpu`
tree are densely commented house-style. This readme is prose, not code, so the no-comment rule does not
bear on it. The standing strip-comments decision for the module is unchanged and still awaits Gabbo's
word (flagged in `2026-06-02-gpu-rhi-m2-team-integration.md` and the D3D12 writeups).

## CLAUDE.md suggestions (recommendations only)
- The `2026-06-02-gpu-rhi-m2-d3d12-phase3.md` writeup recommends the `…_VK_FAILED` → `…_BACKEND_FAILED`
  rename (the public status enums are Vulkan-named but carry D3D12 failures). If taken, the readme's
  diagnostics section should follow. Not done here (touches `src`/`include`, out of scope).
- The spec §3.1 indirect-for-imports family (buffer / texture-view / accel-struct indirect peers) is
  designed but unimplemented; when it lands, the readme binding section's indirect-family note needs
  updating from "sampler only, engine-owned" to the full set.

## Suggestions
- A caps sub-tier distinguishing D3D12 dynamic-resources (`ResourceDescriptorHeap` ceiling) from
  descriptor-table bindless (the floor) — both currently report `root_record` / `descriptor_indices`,
  so a power user cannot branch on which lowering is live. Once it exists, document it in the binding
  section.
- The M1 writeups' `HELLO_GPU_AUTO=triangle` reference is now stale; harmless, but the live screen-name
  set is the one in `apps/hello-gpu/src/main.c`.
