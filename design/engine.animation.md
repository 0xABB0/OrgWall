# Animation System

## Goals

- Fast: SoA batched math, zero malloc in the update loop
- Simple: small composable pieces, no monolithic animator
- Extensible: new types register at startup, game logic stays outside
- Universal: same pipeline for 2D sprites, 2D skeletal, and 3D AAA characters

## Architecture Overview

The animation system is a data transformation pipeline:

```
[Clip]          read-only asset, flat SoA keyframe data
   |
   v
[Sample]        find keyframes, apply easing, interpolate
   |
   v
[Pose]          transient snapshot of evaluated values (scratch arena)
   |
   v
[Blend]         lerp or additive blend multiple poses together
   |
   v
[Extract]       pull values out: floats, vectors, skeleton matrices, root motion
   |
   v
[Game]          sprite frame index, bone transforms, physics delta, GPU upload
```

Each step only knows about its inputs and outputs. No step reaches into another.

## Memory Model

Four tiers, strictly enforced:

**Global static** — the type registry. Initialized once at startup, read-only at runtime. Contains batch function pointers per type.

**Read-only assets** — `Mel_Anim_Clip` and `Mel_Skeleton`. Loaded once, never modified. Multiple threads read concurrently without synchronization.

**Persistent runtime** — `Mel_Anim_Player`. Allocated from the game's persistent allocator. Holds playback cursors, time, speed, event buffer. Lives as long as the entity.

**Transient scratch** — `Mel_Local_Pose` and intermediate arrays. Allocated from a thread-local scratch arena (`Mel_Alloc*`), discarded at end of frame. Zero malloc in the hot loop.

## Data Structures

### Track Group

A track group holds all tracks of one data type within a clip. Grouping by type enables batched SIMD operations.

```c
struct Mel_Track_Group {
    u64 type_hash;
    u32 track_count;

    u64* property_ids;
    u32* keyframe_counts;
    u32* data_offsets;

    f32* flat_times;
    void* flat_values;
    u16* flat_easing_ids;
    f32* flat_easing_params;
};
```

All arrays are tightly packed and contiguous. `data_offsets[i]` gives the starting index into `flat_times` and `flat_values` for track `i`. `flat_easing_ids` stores one easing per keyframe (describes the curve *arriving* at that keyframe). `flat_easing_params` stores optional parameters per keyframe (4 floats each); NULL when no parameterized easings exist.

**Ownership:** all pointers are owned by the clip's asset allocation. Immutable after load.

**Capacity:** `property_ids`, `keyframe_counts`, `data_offsets` are sized to `track_count`. Flat arrays are sized to `sum(keyframe_counts[0..track_count])`. `flat_easing_params` when non-NULL is `4 * sum(keyframe_counts)` floats.

### Event Group

Events are discrete signals that fire at specific times. They are *not* interpolated.

```c
struct Mel_Event_Group {
    u32 track_count;
    u64* property_ids;
    u32* keyframe_counts;
    u32* data_offsets;

    f32* flat_times;
    u64* flat_event_hashes;
};
```

Same layout as track groups but with event hashes instead of interpolatable values. No easing (events are point-in-time). `property_ids` are channel names — a clip can have separate event tracks for different purposes (e.g. `HASH("sfx")` for sound cues, `HASH("hitbox")` for hitbox activation, `HASH("vfx")` for particle spawns). The `property_id` on fired events identifies which channel the event came from.

**Ownership:** same as track group — owned by clip asset allocation, immutable.

### Clip

```c
struct Mel_Anim_Clip {
    u64 name_hash;
    f32 duration;
    bool is_looping;
    u32 additive_space; // MEL_ANIM_ADDITIVE_LOCAL (0) or MEL_ANIM_ADDITIVE_MESH (1)

    Mel_Track_Group* groups;
    u32 group_count;

    Mel_Event_Group* event_groups;
    u32 event_group_count;
};
```

A clip is a read-only asset. Once loaded, it is never modified. Multiple players can reference the same clip concurrently from different threads.

Players reference clips via generational handles (slotmap), not raw pointers. If a clip is unloaded (hot-reload, streaming, level transition), stale handles are detected on lookup rather than dangling silently.

**Thread safety:** fully safe for concurrent reads. No writes after initialization.

### Skeleton

```c
struct Mel_Skeleton {
    u32 bone_count;
    u64* bone_hashes;
    i32* parent_indices;
    u64 root_bone_hash;
};
```

`parent_indices[i]` is -1 for root bones. Parents always precede their children in the array (topological order). `root_bone_hash` identifies the bone whose motion should be extracted for root motion.

**Ownership:** read-only asset, same lifetime rules as clips.

### Pose

The pose is the data bus between pipeline stages. It is a snapshot of evaluated values for one frame. Created, used, and destroyed within a single tick.

```c
struct Mel_Pose_Group {
    u64 type_hash;
    u32 count;
    u64* property_ids;
    void* data;
};

struct Mel_Local_Pose {
    Mel_Pose_Group* groups;
    u32 group_count;
};
```

`data` points to `count * stride` bytes of evaluated results. Allocated 64-byte aligned from scratch for SIMD. The alignment is enforced at allocation time via `mel_arena_push_align`, not via `alignas` on the pointer member.

**Ownership:** all memory comes from the scratch arena passed to `mel_pose_allocate`. The pose and all its inner arrays are transient — valid until the scratch arena resets.

**Thread safety:** a pose is private to the thread that allocated it. No synchronization needed.

```c
void mel_pose_allocate(
    Mel_Local_Pose* out_pose,
    const Mel_Anim_Clip* template_clip,
    Mel_Alloc* scratch
);
```

Allocates the pose structure and all inner groups/arrays from `scratch`, mirroring the clip's group layout.

## Registry

The registry maps type hashes to batch math functions. Initialized once at startup.

```c
typedef void (*Mel_Batch_Lerp_Fn)(
    const void* restrict a,
    const void* restrict b,
    void* restrict out,
    const f32* restrict t,
    u32 count
);

typedef void (*Mel_Batch_Additive_Fn)(
    const void* restrict base,
    const void* restrict additive,
    const void* restrict reference,
    void* restrict out,
    const f32* restrict weight,
    u32 count
);

struct Mel_Track_Type_Def {
    u64 type_hash;
    u32 stride;
    Mel_Batch_Lerp_Fn lerp_fn;
    Mel_Batch_Additive_Fn additive_fn;
};
```

```c
void mel_anim_registry_init(Mel_Alloc* alloc);
void mel_anim_registry_register(u64 type_hash, u32 stride, Mel_Batch_Lerp_Fn lerp_fn, Mel_Batch_Additive_Fn additive_fn);
Mel_Track_Type_Def* mel_anim_registry_get(u64 type_hash);
```

**Built-in types registered at init:**

- `HASH("f32")` — stride 4, `mel_batch_f32_lerp`, `mel_batch_f32_additive`
- `HASH("vec2")` — stride 8, `mel_batch_vec2_lerp`, `mel_batch_vec2_additive`
- `HASH("vec3")` — stride 12, `mel_batch_vec3_lerp`, `mel_batch_vec3_additive`
- `HASH("vec4")` — stride 16, `mel_batch_vec4_lerp`, `mel_batch_vec4_additive`
- `HASH("quat")` — stride 16, `mel_batch_quat_slerp`, `mel_batch_quat_additive`

Games can register additional types (color, matrix, custom).

**Ownership:** the registry's internal array is allocated from the `Mel_Alloc*` passed to init. Lives until engine shutdown.

**Thread safety:** `mel_anim_registry_init` and `mel_anim_registry_register` are not thread-safe (called at startup only). `mel_anim_registry_get` is safe for concurrent reads.

**Failure:** `mel_anim_registry_get` asserts if the type_hash is not found. A missing registration is a programmer error.

## Pipeline

### Sample

```c
void mel_anim_sample(
    const Mel_Anim_Clip* clip,
    f32 current_time,
    u32* cursors,
    Mel_Alloc* scratch,
    Mel_Local_Pose* out_pose
);
```

For each track group in the clip:
1. Look up the batch lerp function from the registry via `type_hash`
2. For each track, advance the cursor to find left/right keyframes bracketing `current_time`
3. Compute `t_local = (current_time - t_left) / (t_right - t_left)`
4. Apply easing: look up `flat_easing_ids[keyframe_index]` in the easing table. If the easing is parameterized (e.g. bezier), read params from `flat_easing_params`. Standard easings use the existing `Mel_Easing_Func` table. The result is `t_eased`.
5. Allocate temporary A, B, T arrays from `scratch`
6. Gather left values into A, right values into B, eased t into T
7. Call the batch lerp function: `lerp_fn(A, B, out_pose->groups[g].data, T, count)`

**Cursors:** flat `u32` array, one per track across all groups. Total size = `sum(clip->groups[i].track_count)`. Cursors are persistent state owned by the player.

**Thread safety:** safe to call concurrently with different cursors/scratch/pose. The clip is read-only.

**Easing resolution:** easing IDs below `MEL_EASING_COUNT` index into the standard easing function table (`mel_ease_linear`, `mel_ease_in_quad`, etc.). Reserved IDs above that range are for parameterized easings (e.g. cubic bezier with 4 control-point floats from `flat_easing_params`).

### Blend (Lerp)

```c
void mel_anim_blend(
    Mel_Local_Pose* pose_a,
    const Mel_Local_Pose* pose_b,
    f32 weight,
    const u64* mask_hashes,
    u32 mask_count
);
```

Blends `pose_b` into `pose_a` in-place. For each matching `type_hash` and `property_id` between the two poses, calls the registry's `lerp_fn` with the given weight.

If `mask_hashes` is non-NULL, only properties whose `property_id` appears in the mask are blended. If NULL, all matching properties are blended.

`weight = 0.0` means 100% pose_a. `weight = 1.0` means 100% pose_b.

**Partial success:** properties that exist in `pose_a` but not `pose_b` (or vice versa) are left unchanged. Only matching pairs are blended.

**Thread safety:** not thread-safe. `pose_a` is written in-place. Both poses must be private to the calling thread.

### Blend (Additive)

```c
void mel_anim_blend_additive(
    Mel_Local_Pose* base_pose,
    const Mel_Local_Pose* additive_pose,
    const Mel_Local_Pose* reference_pose,
    f32 weight,
    const u64* mask_hashes,
    u32 mask_count
);
```

Applies additive blending: `out = base + (additive - reference) * weight`.

The reference pose is typically the bind pose or the first frame of the additive clip. For quaternions: `out = base * slerp(identity, inverse(reference) * additive, weight)`. For scalars/vectors: `out = base + (additive - reference) * weight`.

Writes into `base_pose` in-place. Only properties present in all three poses are processed. Same mask semantics as lerp blend: if `mask_hashes` is non-NULL, only listed properties are blended; if NULL, all matching properties are blended.

**Thread safety:** same as lerp blend — not thread-safe, poses must be thread-private.

### Blend (Multi-way)

Not implemented initially, but the architecture supports it. For blend spaces (1D/2D parameter spaces blending N clips), calling `mel_anim_blend` N-1 times with adjusted weights works but introduces accumulation error for quaternions. A dedicated `mel_anim_blend_multi` would compute a proper weighted average in a single pass:

```c
void mel_anim_blend_multi(
    Mel_Local_Pose** poses,
    const f32* weights,
    u32 count,
    Mel_Local_Pose* out_pose
);
```

Weights must sum to 1.0. Uses the registry's `lerp_fn` iteratively for vectors, and a normalized weighted quaternion approach for quats. Added when blend spaces are needed — no architectural changes required, just a new consumer of the existing registry functions.

### Additive Space

Additive clips can be authored in **local space** (default, described above) or **mesh space** (the additive delta is expressed relative to the model root rather than the parent bone). Mesh-space additive requires transforming the delta through the skeleton hierarchy before applying.

This is distinguished by a flag on the clip (`Mel_Anim_Clip.additive_space`), not by a different blend mode. The `mel_anim_blend_additive` implementation checks this flag and applies the coordinate transform when needed. The skeleton must be available for mesh-space additive blending.

## Player

The player is the convenience layer. It owns persistent playback state and provides high-level operations.

```c
struct Mel_Anim_Clip_State {
    Mel_Anim_Clip_Handle clip;
    f32 time;
    f32 speed;
    u32* cursors;
    u32 cursor_count;
    f32 xfade_elapsed;
    f32 xfade_duration;
};

struct Mel_Anim_Player {
    Mel_Alloc* alloc;
    Mel_Anim_Clip_Pool* clip_pool;

    Mel_Anim_Clip_State* chain;
    u32 chain_count;

    f32 phase;

    Mel_Anim_Event* pending_events;
    u32 event_count;

    Mel_Vec3 prev_root_pos;
    Mel_Quat prev_root_rot;
};
```

`Mel_Anim_Clip_Handle` is a generational slotmap handle. The player resolves it to a `const Mel_Anim_Clip*` when needed (sample, update). If the handle is stale (clip was unloaded), the resolution asserts.

The active clip is always `chain[chain_count - 1]`. Previous entries are clips still fading out.

**Ownership:** the player is allocated by the caller. The `chain` array and each entry's `cursors` are allocated from `alloc` (persistent allocator). `pending_events` is a stretchy buffer via `alloc`. Clip handles are non-owning.

**Crossfade model:** the player holds a dynamic chain of clip states. When `play()` is called with a crossfade, the current clip stays in the chain and the new clip is appended. The blend during sampling is back-to-front: `lerp(lerp(...lerp(chain[0], chain[1], t_01)...), chain[N], t_N)`. This avoids visual pops when rapid transitions overlap.

When a crossfade completes (`xfade_elapsed >= xfade_duration`), that entry and everything before it are pruned from the chain — their cursors are freed. This is the only pruning rule. In practice, chains rarely exceed 2-3 entries.

### Player API

```c
void mel_anim_player_init(Mel_Anim_Player* player, Mel_Alloc* persistent_alloc, Mel_Anim_Clip_Pool* clip_pool);
void mel_anim_player_destroy(Mel_Anim_Player* player);
```

Init zeroes the player and stores the allocator and clip pool reference. The clip pool is the slotmap that owns all loaded clips — the player uses it to resolve handles to `const Mel_Anim_Clip*` internally. Destroy frees internal arrays.

**Destruction:** must not be called while the player's pose or events are still in use. The player does not reference any scratch memory, so scratch lifetime is irrelevant.

```c
typedef struct {
    f32 crossfade;
    f32 start_time;
    f32 speed;
} Mel_Anim_Play_Opt;

void mel_anim_player_play_opt(Mel_Anim_Player* player, Mel_Anim_Clip_Handle clip, Mel_Anim_Play_Opt);

#define mel_anim_player_play(player, clip, ...) \
    mel_anim_player_play_opt((player), (clip), (Mel_Anim_Play_Opt){ .speed = 1.0f, __VA_ARGS__ })
```

Starts playing a clip. If `crossfade > 0`, the current clip remains in the chain as a fading-out source and the new clip is appended. If `crossfade == 0`, the chain is cleared and the new clip becomes the only entry. `start_time` allows starting mid-clip (for motion matching). `speed` defaults to 1.0.

Allocates a new `Mel_Anim_Clip_State` with its cursor array from `player->alloc`.

```c
void mel_anim_player_update(Mel_Anim_Player* player, f32 dt);
```

Advances time on all clip states in the chain by `dt * state.speed`. Handles looping (wraps time for looping clips, clamps for non-looping). Advances crossfade elapsed time on each entry. Prunes completed crossfades (entries where `xfade_elapsed >= xfade_duration` and everything before them). Scans event groups on the active clip (last in chain) and appends fired events to `pending_events` (events whose time falls within `[old_time, new_time)`). Updates `phase` to `active_clip.time / active_clip.duration`.

Events from fading-out clips are *not* fired (matches industry convention: only the incoming clip fires events during crossfade).

`pending_events` is cleared at the start of each `update()` call. Events must be consumed between `update()` calls.

```c
void mel_anim_player_sample(
    Mel_Anim_Player* player,
    Mel_Alloc* scratch,
    Mel_Local_Pose* out_pose
);
```

Allocates poses from `scratch` and evaluates the blend chain. If only one clip is active, samples it directly. If multiple clips are in the chain (overlapping crossfades), samples each into a separate pose and blends back-to-front: the oldest pair is lerped first, then that result is lerped with the next entry, and so on. All intermediate poses are transient scratch allocations.

Asserts that a clip is active (i.e. `play()` has been called). Sampling a player with no clip is a programmer error.

```c
void mel_anim_player_get_float(Mel_Anim_Player* player, u64 property_id, Mel_Alloc* scratch, f32* out);
void mel_anim_player_get_vec2(Mel_Anim_Player* player, u64 property_id, Mel_Alloc* scratch, Mel_Vec2* out);
void mel_anim_player_get_vec3(Mel_Anim_Player* player, u64 property_id, Mel_Alloc* scratch, Mel_Vec3* out);
void mel_anim_player_get_quat(Mel_Anim_Player* player, u64 property_id, Mel_Alloc* scratch, Mel_Quat* out);
```

Convenience: internally calls `sample` + `extract` and returns the value. Allocates a transient pose from `scratch`, which is discarded when the arena resets. Use these when you only need one or two values. For extracting many values, call `sample` once and extract from the pose directly.

These assert that the property exists in the clip.

## Extraction

```c
void mel_pose_extract_float(const Mel_Local_Pose* pose, u64 property_id, f32* out);
void mel_pose_extract_vec2(const Mel_Local_Pose* pose, u64 property_id, Mel_Vec2* out);
void mel_pose_extract_vec3(const Mel_Local_Pose* pose, u64 property_id, Mel_Vec3* out);
void mel_pose_extract_quat(const Mel_Local_Pose* pose, u64 property_id, Mel_Quat* out);
```

Scans pose groups for the matching `property_id`, copies the value into `out`. Asserts if not found.

Scan cost is `O(total_groups)` per call. For a typical clip with 3-5 groups, this is trivial. For bulk extraction (all bones of a skeleton), use `mel_pose_calc_global_matrices` instead.

```c
void mel_pose_calc_global_matrices(
    const Mel_Local_Pose* pose,
    const Mel_Skeleton* skeleton,
    f32* out_4x4_matrices
);
```

Converts local bone transforms (position vec3, rotation quat, scale vec3) from the pose into global 4x4 matrices suitable for GPU skinning. Walks the skeleton hierarchy in parent-first order, multiplying local transforms by parent globals.

`out_4x4_matrices` must point to `skeleton->bone_count * 16` floats. Caller-owned, typically persistent (reused across frames for GPU upload).

Asserts that the pose contains position, rotation, and scale for every bone in the skeleton.

### Root Motion

```c
typedef struct {
    Mel_Vec3 delta_position;
    Mel_Quat delta_rotation;
} Mel_Root_Motion;

void mel_anim_player_extract_root_motion(
    Mel_Anim_Player* player,
    const Mel_Local_Pose* pose,
    const Mel_Skeleton* skeleton,
    Mel_Root_Motion* out
);
```

Computes the root bone's movement delta since the last frame. Reads the root bone's current position/rotation from the pose (identified by `skeleton->root_bone_hash`), computes the delta against the player's stored `prev_root_pos` / `prev_root_rot`, updates the stored values.

The game applies this delta to the character controller / physics body. Optionally, the root bone's contribution can be zeroed from the pose before computing global matrices (so the skeleton doesn't "double-move").

**Call order:** must be called after `mel_anim_player_sample` and before `mel_pose_calc_global_matrices`.

## Events

Events are collected during `mel_anim_player_update` when the playback time crosses an event keyframe. They are stored in the player's `pending_events` array (persistent, stretchy buffer via `player->alloc`).

```c
struct Mel_Anim_Event {
    u64 event_hash;
    u64 property_id;
    f32 time;
};
```

`event_hash` identifies what happened (e.g. `HASH("footstep")`, `HASH("hitbox_on")`). `property_id` identifies the source track. `time` is the exact clip time the event fired at.

Events are cleared at the top of `mel_anim_player_update`. The game must read events between update calls. The ordering of events within a frame is deterministic: sorted by time, then by group order in the clip.

For multithreaded sampling: events are per-player, collected during update (which is per-entity). No cross-entity event interaction during sampling. Cross-entity effects (hit detection, sync groups) are resolved in a separate game-side pass after all entities have updated.

## Easing

Per-keyframe easing is resolved during the sample step. Each keyframe has an `easing_id` that indexes into the standard easing table from `math.easing.h`.

Standard easings (id < `MEL_EASING_COUNT`): `mel_ease_linear`, `mel_ease_in_quad`, etc. No parameters needed.

Step easing: `mel_ease_step` returns 0.0 unconditionally, causing the blend to always pick the left keyframe value. Used for sprite frame indices and other discrete properties.

Parameterized easings (id >= `MEL_EASING_COUNT`): reserved range for easings that read from `flat_easing_params`. Cubic bezier (4 control point floats per keyframe) is the primary use case, needed for Spine-style animation curves.

Easing is applied *before* the batch blend function. The blend function receives `t_eased` values and does pure interpolation. This keeps blend functions simple and allows different easings per property without complicating the math.

## File Structure

```
anim.registry.h / .c       type registration, batch functions
anim.clip.h / .fwd.h / .c  Mel_Anim_Clip, Mel_Track_Group, Mel_Event_Group
anim.skeleton.h / .fwd.h   Mel_Skeleton
anim.pose.h / .fwd.h / .c  Mel_Local_Pose, Mel_Pose_Group, allocation, extraction
anim.pipeline.h / .c       mel_anim_sample, mel_anim_blend, mel_anim_blend_additive
anim.player.h / .fwd.h / .c Mel_Anim_Player, convenience API
```

## Scenarios

### Simple spritesheet

```c
Mel_Anim_Player player;
mel_anim_player_init(&player, persistent_alloc, clip_pool);
mel_anim_player_play(&player, idle_clip_handle);

mel_anim_player_update(&player, dt);
f32 frame;
mel_anim_player_get_float(&player, HASH("frame"), scratch, &frame);
u32 frame_idx = (u32)frame;

if (moving) mel_anim_player_play(&player, walk_clip_handle);
```

A spritesheet loader produces a `Mel_Anim_Clip` with one f32 track (property `HASH("frame")`), step easing, and optional event tracks for sound cues.

### 2D skeletal with events

```c
mel_anim_player_update(&idle_player, dt);
mel_anim_player_update(&attack_player, dt);

Mel_Local_Pose idle_pose, attack_pose;
mel_anim_player_sample(&idle_player, scratch, &idle_pose);
mel_anim_player_sample(&attack_player, scratch, &attack_pose);

u64 upper_body[] = { HASH("bone_torso.rotation"), HASH("bone_arm_l.position") };
mel_anim_blend(&idle_pose, &attack_pose, 1.0f, upper_body, countof(upper_body));

for (u32 i = 0; i < attack_player.event_count; i++)
    if (attack_player.events[i].event_hash == HASH("hitbox_on")) activate_hitbox();
```

### AAA 3D character

```c
mel_anim_player_play(&player, selected_clip_handle, .start_time = match_time, .crossfade = 0.2f);
mel_anim_player_update(&player, dt);

Mel_Local_Pose base;
mel_anim_player_sample(&player, scratch, &base);

Mel_Root_Motion root;
mel_anim_player_extract_root_motion(&player, &base, &skeleton, &root);
apply_to_physics(character, root.delta_position, root.delta_rotation);

Mel_Local_Pose breathe;
mel_anim_player_sample(&breathe_player, scratch, &breathe);
mel_anim_blend_additive(&base, &breathe, &bind_pose, 1.0f);

mel_ik_two_bone(&base, &skeleton, HASH("thigh_l"), HASH("foot_l"), ground_l);
mel_ik_look_at(&base, &skeleton, HASH("head"), look_target);

mel_pose_calc_global_matrices(&base, &skeleton, gpu_matrices);
```

## Out of Scope

Explicitly not part of this module. These are separate systems that consume the animation pipeline's outputs:

**State machines / blend trees** — game-side logic that decides which clips to play and when. The player's `play()` with crossfade is the interface point.

**IK** — post-pose modification. Separate module(s) (`ik.two_bone`, `ik.look_at`, `ik.aim`) that operate on `Mel_Local_Pose*` + `Mel_Skeleton*`.

**Motion matching** — clip selection system. Searches a database, outputs a clip + start time, feeds into `player.play()`.

**Animation compression** — the pipeline is agnostic to storage format. Compressed keyframes (quantized quats, curve fitting) can be decompressed during the sample step without architectural changes. The `flat_values` backing can be swapped.

**Cloth / physics simulation** — reads final bone matrices, writes to separate buffers. No interaction with animation data structures.

**Serialization** — clip loading/saving. Blocked on the serialization system. The data structures are flat and trivially serializable once that system exists.
