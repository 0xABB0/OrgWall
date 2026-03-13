# anim.ik — Inverse Kinematics

## Problem

Melody has a skeletal animation foundation (`anim.skeleton`, `anim.pose`, `anim.player`, `anim.pipeline`) but no way to procedurally adjust bone positions to reach a target. Without IK, characters can't plant feet on uneven terrain, reach for objects, aim weapons, or respond to dynamic targets.

## What This Module Provides

- IK chain definition: references a sub-chain of a skeleton by bone indices
- CCD solver (Cyclic Coordinate Descent): iterative, per-joint rotation toward target
- FABRIK solver (Forward And Backward Reaching IK): two-phase iterative solver, faster convergence on long chains
- Analytic 2-bone solver: closed-form solution for arm/leg (no iteration)
- Solver configuration: max iterations, convergence tolerance, pole vector
- Result status and convergence info

## Inspiration

- **Animation-Engine**: `src/Animation/InverseKinematics/CCD3DSolver.h/.cpp` — per-joint rotation via quaternion from two direction vectors
- **Animation-Engine**: `src/Animation/InverseKinematics/FABRIK3DSolver.h/.cpp` — forward/backward position passes, reconstruct rotations from positions
- **Animation-Engine**: `src/Animation/InverseKinematics/IKChain.h` — chain root→joints→effector with target
- **Animation-Engine**: `src/Animation/InverseKinematics/IKSolver.h` — base with status enum

## File Layout

```
anim.ik.h             — main interface (chain, solver API, result types)
anim.ik.fwd.h         — forward declarations
anim.ik.c             — CCD, FABRIK, analytic solvers
```

---

## Types

### Mel_IK_Chain

```c
typedef struct {
    u32* bone_indices;
    u32 bone_count;
    f32* bone_lengths;
    Mel_Vec3 target;
    Mel_Vec3 pole_vector;
    bool has_pole;
} Mel_IK_Chain;
```

`bone_indices[0]` is the root of the chain, `bone_indices[bone_count - 1]` is the end effector. These are indices into the `Mel_Skeleton` bone array. `bone_lengths` stores the distance between consecutive bones (count = bone_count - 1), precomputed from the rest pose.

The pole vector controls twist — it defines a "hint direction" for the plane of the chain (e.g., which way the elbow points). Only relevant for chains with 3+ bones.

### Mel_IK_Result

```c
#define MEL_IK_CONVERGED  0
#define MEL_IK_PROCESSING 1
#define MEL_IK_FAILED     2

typedef struct {
    u32 status;
    u32 iterations_used;
    f32 final_distance;
} Mel_IK_Result;
```

### Solver functions

All solvers share the same signature pattern: they take the chain definition, read world-space bone positions from the current pose, and write modified local rotations back into the pose.

```c
typedef struct {
    u32 max_iterations;
    f32 tolerance;
} Mel_IK_Solve_Opt;

Mel_IK_Result mel_ik_solve_ccd_opt(
    const Mel_IK_Chain* chain,
    const Mel_Skeleton* skeleton,
    Mel_Local_Pose* pose,
    const f32* global_matrices,
    Mel_IK_Solve_Opt opt);
#define mel_ik_solve_ccd(chain, skel, pose, matrices, ...) \
    mel_ik_solve_ccd_opt((chain), (skel), (pose), (matrices), (Mel_IK_Solve_Opt){ .max_iterations = 10, .tolerance = 0.001f, __VA_ARGS__ })

Mel_IK_Result mel_ik_solve_fabrik_opt(
    const Mel_IK_Chain* chain,
    const Mel_Skeleton* skeleton,
    Mel_Local_Pose* pose,
    const f32* global_matrices,
    Mel_IK_Solve_Opt opt);
#define mel_ik_solve_fabrik(chain, skel, pose, matrices, ...) \
    mel_ik_solve_fabrik_opt((chain), (skel), (pose), (matrices), (Mel_IK_Solve_Opt){ .max_iterations = 10, .tolerance = 0.001f, __VA_ARGS__ })
```

The `global_matrices` parameter comes from `mel_pose_calc_global_matrices`. The solvers read world positions from there, compute new local rotations, and write them back into `pose`. After IK, the caller must recompute global matrices if needed downstream.

### Analytic 2-Bone

```c
Mel_IK_Result mel_ik_solve_2bone(
    const Mel_IK_Chain* chain,
    const Mel_Skeleton* skeleton,
    Mel_Local_Pose* pose,
    const f32* global_matrices);
```

No options needed — 2-bone is always one-shot, no iteration. Uses law of cosines to compute the two joint angles directly. Pole vector determines which of the two possible solutions to pick.

### Chain construction helpers

```c
typedef struct {
    const Mel_Skeleton* skeleton;
    u32 root_bone;
    u32 effector_bone;
    const f32* rest_global_matrices;
    const Mel_Alloc* alloc;
} Mel_IK_Chain_Build_Opt;

void mel_ik_chain_build_opt(Mel_IK_Chain* chain, Mel_IK_Chain_Build_Opt opt);
#define mel_ik_chain_build(chain, ...) mel_ik_chain_build_opt((chain), (Mel_IK_Chain_Build_Opt){__VA_ARGS__})

void mel_ik_chain_destroy(Mel_IK_Chain* chain, const Mel_Alloc* alloc);
```

`mel_ik_chain_build` walks from `effector_bone` up the parent hierarchy to `root_bone`, building the bone index array and computing rest-pose bone lengths. Asserts if `root_bone` is not an ancestor of `effector_bone`.

---

## Pipeline Integration

IK fits into the animation pipeline as a post-processing step:

```
sample clip → blend poses → apply IK → compute global matrices → skinning
```

Typical usage:

```c
mel_anim_player_sample(player, scratch, &pose);
mel_pose_calc_global_matrices(&pose, skeleton, global_matrices);
mel_ik_solve_ccd(&chain, skeleton, &pose, global_matrices, .max_iterations = 15);
mel_pose_calc_global_matrices(&pose, skeleton, global_matrices);
```

The double `calc_global_matrices` is intentional — IK reads from the first computation and modifies local pose, so globals must be recomputed after.

---

## Design Decisions

**Why bone indices instead of pointers to scene nodes?**
The Animation-Engine stores IK chains as scene node pointers because it uses a scene graph. Melody's animation system operates on `Mel_Local_Pose` + `Mel_Skeleton` which are index-based. Staying index-based is consistent and avoids coupling to any scene representation.

**Why pass global_matrices explicitly instead of computing internally?**
The caller usually already has global matrices computed for rendering. Recomputing them inside the solver would be wasteful. Making it explicit also lets the caller control when recomputation happens.

**Why no solver object / state struct?**
CCD and FABRIK are stateless — they take input and produce output. No need for persistent solver state between frames. The Animation-Engine's IKSolver base class with status tracking adds complexity without value when solvers run to completion each frame.

**Why no joint angle limits for v1?**
Joint constraints (hinge limits, cone limits, twist limits) are important but add significant complexity: per-joint constraint data, clamping after each solver iteration, different constraint types for different joints. Worth adding later, not blocking v1.

---

## Improvements Over Animation-Engine

1. Index-based chains instead of scene node pointers
2. Explicit global matrix passing instead of walking scene hierarchy
3. Stateless solvers — no persistent status tracking or solver objects
4. Pole vector support for twist control
5. 3D analytic 2-bone instead of 2D only
6. Rest-pose bone lengths precomputed and stored, not recomputed each frame

---

## Open Questions for Gabbo

1. **Scratch allocation**: CCD modifies the pose in-place iteratively. FABRIK needs scratch arrays for intermediate world positions (one Vec3 per bone in the chain). Should FABRIK take a scratch allocator, or use a small stack-allocated buffer with an assert on max chain length?

2. **Multiple chains**: when solving multiple IK chains on the same skeleton (e.g., both feet), they can interfere. Do we need a multi-chain solver that interleaves iterations, or is sequential solve-chain-A-then-chain-B sufficient?

3. **Partial chain blending**: sometimes you want IK to only partially override the animation (e.g., 80% IK + 20% original animation). This is essentially blending the pre-IK and post-IK poses. Should the solver take a blend weight, or is that an external concern?

4. **Pole vector auto-computation**: computing a good default pole vector from the rest pose would be useful (e.g., for a leg, the pole vector points forward). Should `mel_ik_chain_build` attempt this?
