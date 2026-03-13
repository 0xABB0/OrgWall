# anim.skin — Skeletal Skinning

## Problem

Melody has the skeletal animation foundation: `anim.skeleton` (bone hierarchy with parent indices and hashes), `anim.pose` (local transforms per bone with global matrix computation), `anim.clip` (keyframed data), and `anim.player` (playback with crossfading). But there's no system to actually deform mesh vertices based on the skeleton's current pose. Bones move but nothing visible changes.

## What This Module Provides

- Skin data: per-vertex bone indices + bone weights
- Inverse bind matrices: rest-pose bone transforms, inverted
- Joint matrix computation: `joint_matrix[i] = global_matrix[i] * inverse_bind_matrix[i]`
- GPU skinning buffer preparation: upload joint matrix palette for vertex shader consumption
- CPU skinning path: compute deformed positions/normals directly (for debug, physics queries, raycasting)

## Inspiration

- **Animation-Engine**: `src/Animation/Skin.h` — inverse bind matrices + joint indices, loaded from glTF
- **Animation-Engine**: `src/Components/Animation/SkinReference.h`, `Joint.h` — ECS component linking to skin data
- glTF 2.0 skinning specification

## File Layout

```
anim.skin.h           — main interface (skin data, joint matrix computation, CPU/GPU skinning)
anim.skin.fwd.h       — forward declarations
anim.skin.c           — joint matrix computation, CPU skinning, GPU buffer upload
```

---

## Types

### Mel_Skin

```c
typedef struct {
    Mel_Mat4* inverse_bind_matrices;
    u32* joint_bone_indices;
    u32 joint_count;
} Mel_Skin;
```

`inverse_bind_matrices[i]` is the inverse of the rest-pose global transform of joint `i`. `joint_bone_indices[i]` maps skin joint `i` to a bone index in the `Mel_Skeleton`. These arrays are parallel — same length (`joint_count`).

The skin is asset-lifetime data, loaded once from a model file. The `Mel_Skin` struct is a view — it doesn't own the memory. Ownership belongs to whoever loads the model.

### Mel_Skin_Vertex

Per-vertex bone influence data:

```c
#define MEL_SKIN_MAX_INFLUENCES 4

typedef struct {
    u32 joints[MEL_SKIN_MAX_INFLUENCES];
    f32 weights[MEL_SKIN_MAX_INFLUENCES];
} Mel_Skin_Vertex;
```

4 influences per vertex is the glTF/GPU standard. Weights sum to 1.0. Unused slots have weight 0. `joints` indices reference the skin's joint array (not skeleton bone indices directly).

### Mel_Joint_Palette

The computed per-frame joint matrices, ready for GPU upload:

```c
typedef struct {
    Mel_Mat4* matrices;
    u32 count;
} Mel_Joint_Palette;
```

---

## API

### Joint matrix computation

```c
void mel_skin_compute_palette(
    const Mel_Skin* skin,
    const f32* global_matrices,
    Mel_Joint_Palette* out_palette);
```

For each joint: `out_palette->matrices[i] = global_matrices[skin->joint_bone_indices[i]] * skin->inverse_bind_matrices[i]`

`global_matrices` comes from `mel_pose_calc_global_matrices`. The output palette is what gets uploaded to the GPU.

The caller pre-allocates `out_palette->matrices` (array of `skin->joint_count` Mat4s). This function fills it.

### GPU upload

```c
typedef struct {
    Mel_Gpu_Buffer buffer;
    u32 offset;
    u32 capacity;
} Mel_Skin_Gpu_Buffer;

typedef struct {
    Mel_Gpu_Device* dev;
    u32 max_joints;
    const Mel_Alloc* alloc;
} Mel_Skin_Gpu_Init_Opt;

void mel_skin_gpu_init_opt(Mel_Skin_Gpu_Buffer* buf, Mel_Skin_Gpu_Init_Opt opt);
#define mel_skin_gpu_init(buf, ...) mel_skin_gpu_init_opt((buf), (Mel_Skin_Gpu_Init_Opt){__VA_ARGS__})

void mel_skin_gpu_shutdown(Mel_Skin_Gpu_Buffer* buf);

void mel_skin_gpu_upload(Mel_Skin_Gpu_Buffer* buf, const Mel_Joint_Palette* palette);
```

`Mel_Skin_Gpu_Buffer` is a CPU_TO_GPU buffer that holds joint matrices for the vertex shader. One buffer per skinned mesh instance. `mel_skin_gpu_upload` copies the palette into the mapped buffer.

The vertex shader reads `joint_matrices[vertex.joints[i]]` and applies `sum(weight[i] * joint_matrices[joints[i]] * position)`.

### CPU skinning

```c
void mel_skin_deform_positions(
    const Mel_Joint_Palette* palette,
    const Mel_Skin_Vertex* skin_vertices,
    const Mel_Vec3* rest_positions,
    Mel_Vec3* out_positions,
    u32 vertex_count);

void mel_skin_deform_normals(
    const Mel_Joint_Palette* palette,
    const Mel_Skin_Vertex* skin_vertices,
    const Mel_Vec3* rest_normals,
    Mel_Vec3* out_normals,
    u32 vertex_count);
```

CPU skinning applies the joint palette to vertices on the CPU. For each vertex: `out = sum(weight[i] * palette->matrices[joint[i]] * rest_pos)`. Normals use the inverse-transpose but since joint matrices are rigid transforms (rotation + translation), the rotation part suffices — extract upper-left 3x3 and multiply.

CPU skinning is useful for:
- Physics collision against deformed meshes
- Raycasting against skinned geometry
- Debug visualization
- Platforms without vertex shader support (hypothetical)

### Skin data construction

```c
typedef struct {
    u32 joint_count;
    const Mel_Alloc* alloc;
} Mel_Skin_Alloc_Opt;

void mel_skin_alloc_opt(Mel_Skin* skin, Mel_Skin_Alloc_Opt opt);
#define mel_skin_alloc(skin, ...) mel_skin_alloc_opt((skin), (Mel_Skin_Alloc_Opt){__VA_ARGS__})

void mel_skin_destroy(Mel_Skin* skin, const Mel_Alloc* alloc);
```

Allocates the inverse bind matrices and joint bone indices arrays. The caller fills them in (from a model loader, or manually for testing).

---

## Pipeline Integration

```
sample clip → blend → IK → compute global matrices → compute joint palette → GPU upload → draw
                                                    ↘ CPU skinning (optional, for physics/debug)
```

Typical usage:

```c
mel_anim_player_sample(player, scratch, &pose);
mel_pose_calc_global_matrices(&pose, skeleton, global_matrices);
mel_skin_compute_palette(&skin, global_matrices, &palette);
mel_skin_gpu_upload(&gpu_buf, &palette);
```

The mesh pass vertex shader then reads from the joint buffer and applies skinning per-vertex.

---

## Design Decisions

**Why 4 influences max?**
glTF, Vulkan vertex formats, and GPU shaders universally use 4. It maps to two uvec4 + vec4 vertex attributes. 8 influences exist in some DCC tools but are rare in real-time rendering. We define it as MEL_SKIN_MAX_INFLUENCES so it can be changed, but 4 is the default.

**Why the skin is a view, not owning?**
Skin data is asset-lifetime. It comes from a model file and lives as long as the asset. The skin struct points into that data. Multiple mesh instances can share the same skin data. Making it owning would force copies.

**Why separate palette struct instead of writing directly into the GPU buffer?**
The palette is useful on the CPU too (for CPU skinning, debug viz). Separating computation from upload means the same palette can feed both paths.

**Why linear blend skinning (LBS) and not dual quaternion?**
LBS has known volume-loss artifacts at extreme joint rotations (candy wrapper effect). Dual quaternion skinning (DQS) preserves volume but is more expensive and has its own artifacts. LBS is simpler, well-understood, and sufficient for most cases. DQS can be added as an alternative later.

---

## Improvements Over Animation-Engine

1. Explicit joint palette as an intermediate — usable for both GPU and CPU paths
2. CPU skinning functions for positions AND normals
3. GPU buffer management integrated with Melody's Vulkan pipeline
4. No dependency on a specific model format (glTF) — generic skin data that any loader can populate
5. Separate alloc/destroy for skin data — no hidden allocations

---

## Open Questions for Gabbo

1. **No model loader problem**: skin data must come from somewhere. For testing, we can hand-build a simple skeleton + skin (e.g., a 2-bone arm). But for real content, we need a model loader. Should we prioritize a minimal glTF loader alongside this, or test with synthetic data?

2. **Joint palette lifetime**: the palette is computed per frame. Should it be stack-allocated (small skeletons), arena-allocated per frame, or persistent + overwritten?

3. **Vertex format**: skinning requires bone indices + weights per vertex. How does this integrate with `Mel_Mesh`? Currently Mel_Mesh has positions, normals, colors. We'd need to extend it with skin vertex data — or create a separate `Mel_Skinned_Mesh` type.

4. **Multi-mesh skinning**: one skeleton can drive multiple meshes (e.g., character body + armor + weapon). Each mesh has its own skin vertex data but shares the same joint palette. The current design supports this naturally, but worth confirming.

5. **Shader**: the vertex shader needs to be aware of skinning. Do we create a separate "skinned mesh" shader/pipeline, or add skinning as a permutation of the existing mesh shader?
