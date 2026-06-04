# Slang first-class bindless — Metal alignment (task #37)

Status: research + design complete; implementation is a cross-backend binding-model
change held for a checkpoint (the alignment is too large to force without breaking the
green Vulkan/WebGPU heap). This doc records the exact Slang lowering (empirically
probed against the vendored libslang 2026.10.2) and the alignment plan.

## 0. The wall (reproduced)

The current mandelbrot/clear kernels author an unbounded heap array:

    [[vk::binding(4,0)]] [format("rgba8")] RWTexture2D<float4> u_images[];

Slang's stock MSL emit lowers the unbounded resource array to a flexible-array-member
kernel parameter:

    texture2d<float, access::read_write> u_images_0[];

`xcrun -sdk macosx metal -c` rejects it: `error: flexible array members are a C99
feature` (and `invalid address space qualification for buffer pointee type`). Confirmed
against this exact toolchain (`Apple metal version 32023.830`).

## 1. Slang's first-class bindless: `DescriptorHandle<T>`

`DescriptorHandle<T>` is the only first-class bindless mechanism available in
2026.10.2. Two authoring forms were probed:

- `ResourceDescriptorHeap[i]` — **undefined identifier** (not in this build). Unusable.
- Constructing a handle from a uint in-shader — `DescriptorHandle<T>(uint2(...))`,
  `(DescriptorHandle<T>)uint` — **rejected on Metal**: `DescriptorHandle.init` is an
  "unavailable feature" for the metal compute stage. A handle may therefore NOT be
  synthesized in-shader from an index on Metal.
- A `getDescriptorFromHandle` user override (the documented customization hook) — emits
  on every target but **does NOT relocate the synthesized heap binding** on SPIR-V (stays
  Binding 2) and is **fully ignored on Metal** (resource still inlined). Not a lever for
  retargeting either backend's layout.

The only portable form that compiles to MSL is a `DescriptorHandle<T>` **field of a
host-supplied struct** (here the push-constant `Root`):

    struct Root {
        DescriptorHandle<RWTexture2D<float4>> image;   // first-class bindless handle
        uint w; uint h; uint max_iter;
        float center_x; float center_y; float scale; float time;
    };
    [[vk::push_constant]] Root root;
    ...
    RWTexture2D<float4> img = root.image;   // implicit getDescriptorFromHandle
    img[int2(px)] = ...;

This compiles cleanly to MSL (verified: `xcrun metal -c` produced a `.air`), and emits
valid SPIR-V and WGSL from the SAME source.

## 2. How `DescriptorHandle<T>` lowers per target (empirical)

### Metal / MSL — NO global heap; resource inlined into the argument buffer

The whole `Root` becomes ONE **mixed argument buffer** at `[[buffer(0)]]`. Each
`DescriptorHandle` field becomes the **resolved resource inlined as an argument-buffer
element**; the scalar fields are inline uniform bytes:

    struct Root_0 {
        texture2d<float, access::read_write> image_0;   // arg-buffer resource, id 0
        uint w_0; uint h_0; uint max_iter_0;            // inline uniform bytes
        float cx_0; ...; float time_0;
    };
    [[kernel]] void cs_main(uint3 tid [[thread_position_in_grid]],
                            Root_0 constant* root_1 [[buffer(0)]]) { ... }

Storage buffers lower to `float device*` (a raw device pointer field); samplers to
`sampler`; sampled textures to `texture2d<…, access::sample>`.

Reflection (Metal target) of the `root` argument buffer:

- field `image` — kind=Resource, **uniform size 0**, category 3 (argument-buffer
  element / descriptor-table slot), **element offset 0** → the texture is arg-buffer
  resource id 0.
- fields `w..time` — category 8 (Uniform), byte offsets 0,4,8,12,16,20,24 → **28 bytes**
  of inline uniform data.

**Consequence.** On Metal there is no bind-once global heap to index. The host must
**build the argument buffer per dispatch**: bind the resolved texture's `MTLResourceID`
at argument-buffer id 0 and copy the 28 uniform bytes at their reflected offsets. This is
exactly the merge of "push constants" and "bindless" into one Slang-shaped argument
buffer — which the current Metal RHI does NOT do.

### SPIR-V / Vulkan — uint2 handle + one synthesized global heap

The handle becomes a `uint2` (`v2uint`) inside the push-constant struct
(`Root_std430`, member 0, offset 0, 8 bytes). A compiler-synthesized global
`OpTypeRuntimeArray` heap is declared per SPIR-V resource type; the handle `.x` indexes
it (`OpAccessChain %__slang_resource_heap %handle.x; OpLoad; OpImageWrite`).

Binding assignment (probed, multiple classes in one kernel):

- All non-sampler resources (sampled image, storage image, storage buffer) → **Binding
  2, DescriptorSet 0** (one shared "resource heap" binding, differentiated by SPIR-V
  type, multiple `OpVariable`s at the same binding).
- Samplers → **Binding 0, DescriptorSet 0**.
- `-bindless-space-index N` (`CompilerOptionName::BindlessSpaceIndex`, opt 93) moves the
  **DescriptorSet** (e.g. 3) but keeps Binding {0,2}.

This does NOT match the engine's current 5-binding-per-class layout
(`SAMPLED_IMAGE=0, SAMPLER=1, STORAGE_BUFFER=2, UNIFORM_BUFFER=3, STORAGE_IMAGE=4`). The
current green path sidesteps Slang's synthesis by hand-declaring
`[[vk::binding(4,0)]] RWTexture2D u_images[]` → Binding 4 = the engine's STORAGE_IMAGE
class. `DescriptorHandle` fields cannot carry a `[[vk::binding]]`; the heap is synthesized
and the author cannot place it.

### WGSL — vec2<u32> handle + per-type global arrays at @binding(0) @group(0)

    @binding(0) @group(0) var _slang_resource_heap_0 : array<texture_2d<f32>>;
    @binding(0) @group(0) var _slang_resource_heap_1 : array<texture_storage_2d<...>>;
    ...
    struct R_std430 { wi : texture_storage_2d<...>, ... };   // handle = .x index
    textureStore(_slang_resource_heap_1[r.wi.x], ...);

Note WebGPU core has no true bindless heap (per design §P1 honest gating); the engine's
WebGPU heap is already a known gap, so the WGSL form is informational here.

## 3. Alignment plan (the cross-backend change — checkpoint)

Three coupled changes are required; none is Metal-only.

### 3.1 Consumer ABI: `uint` slot → `DescriptorHandle` field

`root.image` changes from a `uint` (a heap index the shader dereferences) to a
`DescriptorHandle<RWTexture2D<float4>>` field. The host fills it differently per backend:

- Vulkan/WGSL: write `uint2(slot, 0)` into the field (slot == handle.index, §3.1 of the
  RHI binding model preserved as the `.x` component).
- Metal: write the resolved resource (`MTLResourceID`) into the argument-buffer element;
  the engine must look up `slot → texture view → gpuResourceID` at dispatch time.

`bindless_present.c`'s `{ u32 tex; u32 smp; }` root similarly becomes
`{ DescriptorHandle<Texture2D> tex; DescriptorHandle<SamplerState> smp; }`.

### 3.2 Metal RHI: argument-buffer-per-dispatch (replaces 5-heap + setBytes)

The current Metal path is incompatible:

- push constants ride `setBytes` at `[[buffer(0)]]` as a raw byte struct;
- 5 global heaps bind at `[[buffer(1..5)]]` as `MTLResourceID`/`gpuAddress` tables the
  shader would index by slot.

Slang's MSL expects neither. It wants ONE argument buffer at `[[buffer(0)]]` mixing the
resolved resource(s) and the inline uniform bytes. Required:

1. At pipeline-create from Slang, capture the Metal-target reflection of the entry
   point's argument buffer: per-field category (resource element vs uniform), element
   id, and uniform byte offset/size.
2. At dispatch, allocate a transient argument buffer (`MTLArgumentEncoder` from the
   compute function's `[[buffer(0)]]` argument, or — tier-2 — direct writes using the
   reflected ids/offsets). For each `DescriptorHandle` field: resolve slot →
   live resource → `setTexture:atIndex:id` / `setBuffer` into the encoder; for the
   uniform region copy the push-constant bytes at the reflected offset.
3. Bind that argument buffer at `[[buffer(0)]]`; drop the separate
   `cmd_bind_bindless` heap binds for the Slang path.
4. `MTLResidencySet` is still needed so the inlined resources are resident (the argument
   buffer references them by id).

The existing 5-global-heap `binding.m` stays for the manual/non-Slang bindless surface
(test_metal.c) but is no longer what the Slang screens use. This is the largest piece and
the reason for the checkpoint: it reshapes how `cmd_push_constants` + bindless interact on
the compute and render encoders.

### 3.3 Vulkan/WGSL RHI: re-lay-out the heap to Slang's {sampler@0, resource@2}

`binding.c`'s 5-binding descriptor-set layout must change to match Slang's synthesized
heap: one combined resource binding (Binding 2) holding sampled images, storage images,
and storage buffers as a single update-after-bind runtime array (differentiated by Vulkan
descriptor type within the binding — needs `VK_DESCRIPTOR_TYPE` per `OpVariable`, which
Slang emits as distinct same-binding variables, so the set layout needs the union), plus
a sampler binding (Binding 0). Use `BindlessSpaceIndex` to pin the set to the engine's
bindless set (0). Slot semantics (slot == handle.index) are preserved as the `.x`
component. Uniform-buffer-as-DescriptorHandle is currently rejected by Slang
(`ConstantBuffer` handle assignment type-mismatch) — that class stays on the classic path.

This is the **flagged cross-backend change**: it rewrites the working Vulkan heap that
gpu-vulkan 48/48 depends on. It must be done with the Vulkan suite kept green, which is a
milestone of its own.

## 4. Wrapper (third-party/slang) change needed

`mel_slang_compile_reflect` currently passes NO compiler options and exposes no
Metal-argument-buffer reflection. To implement §3, the wrapper must:

- accept a `bindless_space_index` option (→ `CompilerOptionName::BindlessSpaceIndex`) so
  Vulkan can pin the synthesized heap's descriptor set;
- expose the Metal-target argument-buffer reflection (per-field category=resource|uniform,
  element id, uniform offset/size) so the Metal RHI can build the argument buffer.

Both are additive to `compile.h` (new fields, no ABI break to existing callers).

## 5. Why this is a checkpoint, not a one-pass fix

The Metal fix (§3.2) cannot land without the consumer ABI change (§3.1), which cannot land
without the Vulkan/WGSL heap re-layout (§3.3) — otherwise the SAME re-authored
`DescriptorHandle` shader breaks the green Vulkan path (its heap lands at Binding 2, not
the engine's Binding 4). The three are coupled; doing only Metal would author a shader
that no longer matches the Vulkan heap. Per the task's stop condition, the binding-model
change spans every backend and is held for Gabbo's checkpoint.

## 6. Verified facts (toolchain `Apple metal 32023.830`, libslang 2026.10.2)

- Current `u_images[]` MSL: REJECTED by `xcrun metal -c` (flexible array member). [the wall]
- `DescriptorHandle<RWTexture2D>` field MSL: COMPILES (`.air` produced).
- Same source → valid SPIR-V and WGSL.
- Metal handle → inlined arg-buffer resource id 0 + 28 uniform bytes (reflected).
- SPIR-V handle → uint2 in push constant; heap at set 0, binding 2 (resources) / 0
  (samplers); `BindlessSpaceIndex` moves the set only.
- `ResourceDescriptorHeap` unavailable; in-shader handle construction rejected on Metal;
  `getDescriptorFromHandle` override does not relocate the heap.
