# GPU RHI M2 — finishing the binding model (A1 reflection, A2 classic path, A4 sampler, U13 compute floor)

Continues the GPU RHI rewrite (`design/gpu-rhi.md`) from where the binding model stopped
(`writeup/2026-06-02-gpu-rhi-m2-binding-model.md`). That session landed U11 samplers, the U14 bindless heap
floor + BDA ceiling, and U12 reflection-lite, but left a confessed worklist: `MissingBindlessSlot` was dead
code, reflection read no descriptor bounds / vertex input / spec constants, set-0 use was force-marked
bindless (foreclosing a classic set 0), static-sampler lifetime was unenforced, and the storage-buffer /
uniform / storage-image heap classes were unproven. This session **finishes the binding model correctly**
(the worklist's section A, minus the codegen-blocked mixin) and then takes the first **proceed-forward**
step (U13 compute) to prove storage-buffer bindless.

Runnable half is Vulkan/macOS over MoltenVK, headlessly verified. **gpu-vulkan 24/24, gpu-foundation 8/8,
collection-slotmap 3/3, zero validation errors, zero leaks, zero VUIDs.**

## Work done

### A1 — reflection upgrade (`reflect.c`, `vk_backend.h`, `shader.c`, `pipeline.c/.h`)
The single-pass SPIR-V reader is now an **accumulator** (`mel_gpu__spirv_reflect(code, bytes, vertex_stage,
alloc, accum)`) producing, as the vs+fs (or cs) union:
- **Set-0 descriptor bounds** — per binding, the declared array length and whether it is an
  `OpTypeRuntimeArray`. `uses_bindless_set` is now precisely *"set 0 declares ≥1 runtime descriptor array"*
  (the update-after-bind heap signature), not *"any set-0 use."* This single change un-forecloses the
  classic set-0 path (the A2 separation, below).
- **`MissingBindlessSlot` is now reachable.** A bindless pipeline whose shader declares a **sized** set-0
  descriptor array longer than the heap's class capacity (heap class = binding index, the engine
  convention) fails create with `MissingBindlessSlot{binding, demand}`, distinct from `MissingFeature`.
  Runtime arrays never trip it (the partially-bound heap satisfies them).
- **Vertex-input layout** — vertex-stage `Input` variables with a `Location` (builtins carry `BuiltIn` and
  are skipped) become a single interleaved binding, sorted by location, tight-packed, stride summed.
  `pipeline_create` uses it as the default when the caller passes no explicit `vertex_layout` (§6.5
  reflection-default / manual-override); fullscreen-triangle shaders reflect zero attributes and behave
  exactly as before.
- **Specialization constants** — `OpSpecConstant{,True,False}` with a `SpecId` are recorded (id + scalar
  size); `pipeline_create.spec_constants[]` (`{id, u32 value}`) builds one `VkSpecializationInfo` shared by
  both stages. A supplied id the shader does not declare warns (MEL-CODE-007), not silently no-ops.

The reflection now owns three small dynamic arrays, freed by `mel_gpu__reflection_free` at shader destroy.

### A2 — classic descriptor-set path, distinct from the heap (`bind_group.{h,c}`, `pipeline.c`, `device.c`)
The P2 peer of the bindless heap (§6.7), now a real subsystem:
- `Mel_Gpu_Bind_Group_Layout` (one `VkDescriptorSetLayout` + an owned copy of its entries) and
  `Mel_Gpu_Bind_Group` (one `VkDescriptorSet`), with `mel_gpu_bind_group_write_{texture,sampler,combined,
  buffer}` and `mel_gpu_cmd_bind_descriptor_set(cmd, set_index, group)`.
- A **grown-on-demand classic descriptor-pool chain** on the device (a new pool when one is exhausted —
  `FREE_DESCRIPTOR_SET_BIT` so a group frees its set). Set frees are **future-gated** through the existing
  deferred-free watermark (a new `descriptor_set`/`descriptor_set_pool` pair on `Mel_Gpu_Deferred_Free`).
- `pipeline_create.set_layouts[]` builds a non-bindless pipeline layout from app-owned set layouts at sets
  0..N-1 (mutually exclusive with `bindless`; static-sampler set follows). Because reflection now tells a
  runtime-array set 0 from a sized one, a classic set-0 shader is **no longer force-marked bindless** — the
  test proves it by creating + rendering on a device with **no heap at all**.

### A4 — sampler caps + static-sampler lifetime (`caps.h`, `device.c`, `sampler.c`, `pipeline.c`, `vk_backend.h`)
- **`caps.sampler` domain** (`anisotropy`, `max_anisotropy`) populated from the device limit; the sampler
  create's anisotropy clamp is this value.
- **Static-sampler lifetime is now engine-enforced.** `pipeline_create` takes one refcount claim per static
  sampler (`mel_gpu__sampler_retain`) and stores the handles on the pipeline; `pipeline_destroy` releases
  them **after** deferring the pipeline, so a sampler whose last claim was the pipeline retires no earlier
  than the layout that baked it. Destroying the user's handle under a live pipeline no longer frees the
  `VkSampler` (was the caller's unenforced contract).

### B / U13 — compute pipeline floor + storage-buffer bindless proof (`shader.{h,c}`, `pipeline.{h,c}`, `command.{h,c}`, `binding.c`, `bind_group.c`, `vk_backend.h`)
- `mel_gpu_shader_create_compute_from_bytecode` (single-stage SPIR-V; reflection with `vertex_stage=false`)
  and `mel_gpu_pipeline_compute_create` (reflection-driven layout, the same `MissingFeature` /
  `MissingBindlessSlot` gates, spec constants, bindless set 0 or classic `set_layouts`, `COMPUTE`
  push-constant stage, `vkCreateComputePipelines`).
- The command list now tracks the bound pipeline's **bind point** and **push-constant stage mask**;
  `cmd_bind_pipeline`, `cmd_push_constants`, `cmd_bind_bindless`, and `cmd_bind_descriptor_set` all follow
  it, so graphics and compute share one recording path. `cmd_dispatch` added.
- **Storage-buffer bindless is proven end-to-end**: a compute shader reads one heap-resident storage buffer
  and writes another, both addressed purely by bindless slot indices in a push-constant root record;
  `out[i] == in[i] + 1` is read back on the CPU. This is the heap class the graphics tests could not reach.

## Verification
- `DYLD_LIBRARY_PATH=/opt/homebrew/lib MEL_TEST_NOFORK=1 ./nob test gpu-vulkan macos --gpu=vulkan` —
  **24/24** (the prior 18 plus six new):
  - `vk_bindless.missing_bindless_slot` — a `texture2D[20000]` at set 0 binding 0 (cap 16384) ⇒
    `MissingBindlessSlot`, not `MissingFeature`, not a crash, no VUID.
  - `vk_pipeline.spec_constants_bake` — baking `(0.5, 0.25)` paints the target that colour (default black);
    pixel-verified.
  - `vk_pipeline.reflection_vertex_input` — a pos(vec2)+color(vec3) vertex shader with **no** explicit
    layout; reflection derives stride 20; the covering triangle's colour is pixel-verified.
  - `vk_bind_group.classic_descriptor_set` — a combined-image-sampler at set 0 renders through an app-owned
    bind group on a device with **no heap** (proving the set-0 shader is not force-marked bindless);
    pixel-verified.
  - `vk_bindless.static_sampler_lifetime` — the user's handle destroy leaves the sampler alive (pipeline's
    claim); the pipeline destroy retires it. `alive()` transitions verified, leak-free.
  - `vk_compute.storage_buffer_bindless` — the compute add kernel; `out[i] == in[i]+1` verified.
- `./nob test gpu-foundation` — **8/8**. `./nob test collection-slotmap` — **3/3**.
- `HELLO_GPU_AUTO=cube` runs for seconds with **zero validation errors** through the now reflection-aware
  graphics `pipeline_create` (the reflection-derived vertex-input default does not engage — the cube passes
  an explicit layout).
- The full gpu-vulkan run is grep-clean of `leak` / `VUID` / `validation error` / unexpected `ERROR` (the
  only logged errors are the intentional `MissingFeature` / `MissingBindlessSlot` diagnostics from the
  negative tests, MEL-ENGINE-VIII).

## Kludges and debt (confessed, MEL-ENGINE-VIII)

### A1 reflection
- **Vertex-input formats are float vec2/3/4 only** (the current `Mel_Gpu_Format` enum). A scalar-float or
  integer input has no enum entry: reflection logs a warning and **withdraws the whole derived layout** so
  the caller must supply an explicit one (no half-correct layout). Single interleaved binding, `VERTEX`
  input rate, tight packing — no per-instance rate, no multiple bindings, no explicit offsets/gaps.
- **Spec constants are 4-byte scalars.** The public API value is `u32`; an 8-byte spec constant warns and
  bakes only the low 4 bytes. Bool spec constants are treated as 4-byte `VkBool32`.
- **Set-0 class mapping is by binding number** (binding 0 = sampled image … 4 = storage image), the engine
  heap convention. Correct for the heap; a classic set 0 uses its own explicit layout and does not route
  through this. `MissingBindlessSlot` is reachable only via a **sized** over-cap array; a runtime array can
  never name a static slot, so over-indexing a runtime array at draw time is still GPU-AV territory, not a
  create-time error. 64-bit array lengths still read the low word only; multiple push-constant blocks still
  take a max-size union.

### A2 classic path
- `Mel_Gpu_Descriptor_Kind` is an **enum** (MEL-CODE-001) — protocol mapping onto `VkDescriptorType`, the
  same carve-out and Rule-#1 flag as the format/state/sampler enums.
- **No reflection-derived classic layout.** The user declares the bind-group layout explicitly; deriving it
  from a sized set-0 shader's reflection (descriptor *types*, which reflection does not yet capture — it
  records bounds, not kinds) is additive and deferred. So the §6.7 "reflection tells U13 the descriptor-table
  layout" convenience is half-present (bounds yes, types no).
- **Classic pool stride is fixed per pool** (256 sets, 256 descriptors/type) but the **chain grows**, so it
  is a stride not a ceiling (MEL-CODE-002 respected). Bind-group **writes are unlocked** — the caller
  serializes per-set writes (§3.7), same contract as the heap. A bind-group layout's `entries` copy leaks
  only if the layout itself is leaked at device destroy (already a flagged error).
- Mutable descriptor types (`VK_EXT_mutable_descriptor_type`) are **not** used: each class is its own
  binding.

### A4
- `caps.sampler` carries only `anisotropy` + `max_anisotropy` — the minimal §6.3 surface, not the full
  sampler-feature domain.
- **Mutable descriptor types, `CAPTURE_REPLAY` through heap creation, and the YCbCr conversion sampler are
  deferred** — all three are cap-absent on MoltenVK (`mutable_descriptor_type` / `descriptorBufferCapture
  Replay` / sampler-YCbCr not exposed), so they cannot be faithfully verified on this host. Implementing
  them blind would be an unverified default; tracked for a device that grants them.

### B / U13 compute
- **`pipeline_compute_create` duplicates** the binding-model gate + spec-info + set-layout-composition logic
  from `pipeline_create` (MEL-ENGINE-IX). It should be factored into a shared
  `mel_gpu__build_pipeline_layout(opt-subset, refl)` helper; deferred to avoid a larger refactor mid-slice.
- **Compute is a floor, not the full U13/U25 surface.** No compute caps (subgroup size control, atomics by
  type, matmul-profile / matrix scope), no `cmd_dispatch_indirect` / `_indirect_count`, no compute static
  samplers (the compute opt has no `static_samplers`), no async-compute queue (dispatch rides the graphics
  queue; single-queue retirement watermark still holds).
- **Only storage-buffer bindless is now proven.** Uniform-buffer and storage-image heap classes are
  registered at resource create but still lack a dedicated pixel test (uniform needs a UBO-read kernel;
  storage-image needs a compute image-write + readback). The class the prior writeup flagged is closed; two
  remain.
- The compute output buffer is **`HOST_COHERENT` + read after the submission's completion future** — no
  explicit `HOST_READ` barrier. Valid (the completion is a memory dependency; sync-validation is not enabled
  in these tests), but a `cmd_buffer_barrier` to a `Host` state would be the explicit form once that state
  exists in the enum.

### Not done — `melody.binding` Slang mixin (A3)
The named M2 deliverable (`RootRecord<T>` / `BindlessResource<T>`, one declaration → both payload forms)
**rides the vendored-Slang / codegen path, which CLAUDE.md marks undocumented and requires halt-and-query
before depending on.** Not attempted. The duality remains demonstrated by separate hand-authored shaders
(index floor vs. BDA ceiling), not one authored declaration. **Needs Gabbo's go-ahead on the codegen pass.**

### Process
- Changes are **uncommitted** on the worktree branch `worktree-gpu-rhi-m2-binding-finish` (commit only when
  asked). The two-phase slotmap removal from the prior session is still the right primitive for §3.3's
  command-pool / transient-ring resets, still un-adopted there.

## CLAUDE.md / repo-convention suggestions (recommendations only)
- The `size`-typedef trap did not bite (byte counts named `bytes` / `size_bytes` / `range`). The hygienic
  `countof` / `isize`-rename recommendation from prior writeups still stands.
- A shared `mel_gpu__build_pipeline_layout` helper would retire the graphics/compute duplication above and
  is the natural home for mesh/RT/tess pipeline layouts when they land.
- `modules/gpu/readme.md` is still absent (flagged twice now). The binding-model conventions belong there:
  set 0 = heap (runtime arrays), binding index = heap class, `slot == handle.index`, BDA = pointer payload,
  classic path = explicit `set_layouts` + bind groups, reflection-derived vertex input default.

## Suggestions
- Next binding-model step: teach reflection to capture descriptor **types** (not just bounds) so the classic
  layout can be reflection-derived, then the `melody.binding` mixin (after the codegen go-ahead) so one
  declaration emits both payload forms.
- Next compute step: uniform-buffer and storage-image bindless pixel tests, then `cmd_dispatch_indirect` and
  the U25 compute caps block (subgroup size control, atomics-by-type, matmul profile) — GPU-driven root
  records (`root_record_update = gpu_generated`) want a compute pass writing a root-record buffer.

## Shader sources (for `modules/gpu/test/bindless_spv.h` regeneration)
Appended this session (regenerate: `glslc -fshader-stage={frag,vert,comp} -mfmt=c <src>` then wrap as
`static const uint32_t NAME_SPV[] = { … };`):

```glsl
// oversize.frag — sized set-0 descriptor array longer than the heap cap ⇒ MissingBindlessSlot
#version 460
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 0, binding = 0) uniform texture2D u_textures[20000];
layout(set = 0, binding = 1) uniform sampler   u_samplers[];
layout(push_constant) uniform Root { uint tex; uint smp; } root;
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;
void main() { o_color = texture(sampler2D(u_textures[root.tex], u_samplers[root.smp]), v_uv); }
```
```glsl
// spec.frag — colour driven by specialization constants (default 0)
#version 460
layout(constant_id = 0) const float c_r = 0.0;
layout(constant_id = 1) const float c_g = 0.0;
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;
void main() { o_color = vec4(c_r, c_g, 0.0, 1.0); }
```
```glsl
// vtxrefl.vert / vtxrefl.frag — reflection-derived vertex input (pos vec2 @0, color vec3 @1)
#version 460
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec3 a_color;
layout(location = 0) out vec3 v_color;
void main() { v_color = a_color; gl_Position = vec4(a_pos, 0.0, 1.0); }
// ---
#version 460
layout(location = 0) in  vec3 v_color;
layout(location = 0) out vec4 o_color;
void main() { o_color = vec4(v_color, 1.0); }
```
```glsl
// classic.frag — sized (non-runtime) combined image sampler at set 0 ⇒ NOT force-marked bindless
#version 460
layout(set = 0, binding = 0) uniform sampler2D u_tex;
layout(location = 0) in  vec2 v_uv;
layout(location = 0) out vec4 o_color;
void main() { o_color = texture(u_tex, v_uv); }
```
```glsl
// add.comp — storage-buffer bindless: out[i] = in[i] + 1, buffers addressed by heap slot
#version 460
#extension GL_EXT_nonuniform_qualifier : require
layout(set = 0, binding = 2) buffer Buf { uint v[]; } u_buffers[];
layout(push_constant) uniform Root { uint in_buf; uint out_buf; uint n; } root;
layout(local_size_x = 64) in;
void main() {
    uint i = gl_GlobalInvocationID.x;
    if (i >= root.n) return;
    u_buffers[nonuniformEXT(root.out_buf)].v[i] = u_buffers[nonuniformEXT(root.in_buf)].v[i] + 1u;
}
```
