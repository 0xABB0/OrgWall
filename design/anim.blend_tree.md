# anim.blend_tree — Hierarchical Animation Blend Trees

## Problem

Melody's current animation blending (`anim.pipeline`) is flat — you blend two poses with a weight, or apply additive blending. This works for simple crossfades but can't express "blend walk/run by speed (1D), then blend that with strafe by direction (2D), then layer upper-body aim on top." Modern character animation needs a tree of blend operations that compose hierarchically.

## What This Module Provides

- Blend tree structure: nodes that produce poses, with children feeding into them
- Blend1D node: scalar parameter, finds two nearest children, interpolates
- Blend2D node: 2D parameter space, Delaunay triangulation, barycentric interpolation
- Clip node (leaf): wraps an animation player, samples at current time
- Additive node: applies one subtree as an additive layer on another
- Per-bone blend masks
- Bottom-up tree evaluation

## Inspiration

- **Animation-Engine**: `src/Animation/Blending/IBlendNode.h` — base node with child list, output pose
- **Animation-Engine**: `src/Animation/Blending/Blend1D.h` — scalar param, segment search, normalized weight
- **Animation-Engine**: `src/Animation/Blending/Blend2D.h` — Delaunay triangulation, barycentric coords
- **Animation-Engine**: `src/Animation/Blending/BlendAnim.h` — leaf wrapping an animation
- **Animation-Engine**: `src/Animation/Blending/BlendingCore.h` — blend_pose_lerp (with mask), blend_pose_barycentric

## File Layout

```
anim.blend_tree.h      — main interface (node types, tree struct, eval API)
anim.blend_tree.fwd.h  — forward declarations
anim.blend_tree.c      — tree evaluation, node dispatch, 1D/2D blending
anim.blend_tree.tri.c  — Delaunay triangulation for Blend2D
```

---

## Types

### Node types (function pointer dispatch, no enums)

Each node type is defined by a vtable of two functions:

```c
typedef void (*Mel_Blend_Node_Eval_Fn)(
    Mel_Blend_Tree* tree,
    Mel_Blend_Node* node,
    const Mel_Alloc* scratch);

typedef struct {
    Mel_Blend_Node_Eval_Fn eval;
} Mel_Blend_Node_Vtable;
```

### Mel_Blend_Node

```c
typedef struct Mel_Blend_Node Mel_Blend_Node;

struct Mel_Blend_Node {
    const Mel_Blend_Node_Vtable* vtable;
    Mel_Local_Pose* output_pose;
    u32 first_child;
    u32 child_count;
    Mel_Vec2 blend_pos;
    union {
        Mel_Blend_Clip_Data clip;
        Mel_Blend_1D_Data blend_1d;
        Mel_Blend_2D_Data blend_2d;
        Mel_Blend_Additive_Data additive;
    };
};
```

Nodes are stored in a flat array inside the tree. `first_child` and `child_count` index into a separate children-index array (indirection table), so children are contiguous per-node but nodes themselves can be any shape.

### Node-specific data

```c
typedef struct {
    Mel_Anim_Player* player;
} Mel_Blend_Clip_Data;

typedef struct {
    f32 blend_param;
} Mel_Blend_1D_Data;

typedef struct {
    Mel_Vec2 blend_param;
    u32* triangles;
    u32 triangle_count;
} Mel_Blend_2D_Data;

typedef struct {
    f32 weight;
    u64* mask_hashes;
    u32 mask_count;
} Mel_Blend_Additive_Data;
```

### Mel_Blend_Tree

```c
typedef struct {
    Mel_Blend_Node* nodes;
    u32 node_count;
    u32 node_capacity;

    u32* children_indices;
    u32 children_count;
    u32 children_capacity;

    u32 root;

    Mel_Local_Pose* scratch_poses;
    u32 scratch_pose_count;

    const Mel_Alloc* alloc;
} Mel_Blend_Tree;
```

The tree pre-allocates scratch poses during init based on the maximum tree depth. During evaluation, each non-leaf node gets a scratch pose to write into. This avoids per-frame allocations.

---

## API

### Tree lifecycle

```c
typedef struct {
    u32 initial_capacity;
    const Mel_Anim_Clip* pose_template;
    const Mel_Alloc* alloc;
} Mel_Blend_Tree_Init_Opt;

void mel_blend_tree_init_opt(Mel_Blend_Tree* tree, Mel_Blend_Tree_Init_Opt opt);
#define mel_blend_tree_init(tree, ...) mel_blend_tree_init_opt((tree), (Mel_Blend_Tree_Init_Opt){__VA_ARGS__})

void mel_blend_tree_shutdown(Mel_Blend_Tree* tree);
```

`pose_template` is a clip that defines the pose layout (which groups/properties exist). All scratch poses are allocated to match this layout.

### Building the tree

```c
u32 mel_blend_tree_add_clip(Mel_Blend_Tree* tree, Mel_Anim_Player* player, Mel_Vec2 blend_pos);
u32 mel_blend_tree_add_blend1d(Mel_Blend_Tree* tree);
u32 mel_blend_tree_add_blend2d(Mel_Blend_Tree* tree);
u32 mel_blend_tree_add_additive(Mel_Blend_Tree* tree, f32 weight);

void mel_blend_tree_set_root(Mel_Blend_Tree* tree, u32 node);
void mel_blend_tree_add_child(Mel_Blend_Tree* tree, u32 parent, u32 child);
```

Each `add_*` returns a node index. After building the tree structure, call:

```c
void mel_blend_tree_compile(Mel_Blend_Tree* tree);
```

This resolves the children indirection table, allocates scratch poses based on depth, and for Blend2D nodes, runs Delaunay triangulation on child positions. Must be called before evaluation. Must be re-called if children are added/removed.

### Setting parameters

```c
void mel_blend_tree_set_1d_param(Mel_Blend_Tree* tree, u32 node, f32 param);
void mel_blend_tree_set_2d_param(Mel_Blend_Tree* tree, u32 node, Mel_Vec2 param);
void mel_blend_tree_set_additive_weight(Mel_Blend_Tree* tree, u32 node, f32 weight);
void mel_blend_tree_set_additive_mask(Mel_Blend_Tree* tree, u32 node, const u64* mask_hashes, u32 count);
```

### Evaluation

```c
void mel_blend_tree_evaluate(Mel_Blend_Tree* tree, const Mel_Alloc* scratch);
const Mel_Local_Pose* mel_blend_tree_result(const Mel_Blend_Tree* tree);
```

`evaluate` walks the tree bottom-up. Each leaf samples its player. Each interior node blends its children's poses. The root node's output_pose is the final result, retrieved with `mel_blend_tree_result`.

---

## Blend1D Algorithm

1. Children are sorted by `blend_pos.x` during compile
2. Given `blend_param`, find the two children whose positions bracket it (segment search)
3. Normalize the parameter within that segment: `t = (param - left.x) / (right.x - left.x)`
4. Blend the two children's output poses with weight t using `mel_anim_blend`

If param is outside the range, clamp to the nearest endpoint.

## Blend2D Algorithm

1. During compile, run Delaunay triangulation on children's `blend_pos` XY positions
2. Store resulting triangle indices in `Mel_Blend_2D_Data.triangles`
3. During eval, find which triangle contains `blend_param`
4. Compute barycentric coords (a0, a1, a2) within that triangle
5. Blend the three corner poses: result = a0*pose0 + a1*pose1 + a2*pose2

Triangle search is linear (iterate triangles, test point-in-triangle). For small child counts (typically 4-8 clips in a 2D blend), this is fine.

## Delaunay Triangulation

Implemented in `anim.blend_tree.tri.c`. Incremental algorithm operating on 2D points. Output: array of u32 triples (vertex indices into the children array). This is internal to Blend2D — not exposed as a general-purpose math utility.

If we later need Delaunay for other purposes, we can extract it to `math.geo.delaunay.*`.

---

## Design Decisions

**Why flat array of nodes instead of pointer-based tree?**
Cache-friendly, single allocation, easy to serialize. Children are referenced by index through an indirection table. This is how most production animation systems store blend trees.

**Why compile step?**
Separates tree construction from evaluation. Compile resolves the indirection table, validates structure, and builds Delaunay triangulations. This upfront cost avoids per-frame overhead.

**Why pre-allocated scratch poses?**
Blend tree evaluation produces intermediate poses at every interior node. Allocating these per-frame would be wasteful. Pre-allocation based on tree depth means zero allocations during evaluation.

**Why Delaunay is internal to blend_tree, not in math.geo?**
Delaunay triangulation on a handful of 2D points is simple enough to inline. It's only used by Blend2D. Exposing it as general math would require more rigorous edge-case handling and a richer API that we don't need yet.

**Why nodes own pointers to Mel_Anim_Player instead of embedding them?**
The player lifecycle is managed externally (by the game/animation controller). The blend tree is a view over existing players, not an owner. Multiple trees could reference the same player.

---

## Improvements Over Animation-Engine

1. Flat node array instead of heap-allocated polymorphic node pointers
2. Pre-allocated scratch poses instead of per-node embedded poses
3. Compile step for upfront validation + Delaunay
4. Additive node as first-class (they only had lerp + barycentric)
5. Integration with Melody's existing `mel_anim_blend` and `mel_anim_blend_additive`
6. Blend masks integrated into additive nodes

---

## Open Questions for Gabbo

1. **Sync groups**: should clips in a 1D blend synchronize their playback phase? (e.g., walk at 1.0s and run at 0.7s both at the "left foot strike" moment). This requires the tree to drive player time, which changes the player ownership model.

2. **Lazy triangulation**: should Blend2D re-triangulate when children are added/removed at runtime, or is compile-once sufficient?

3. **Pose template**: requiring a clip as pose template during init is awkward if the tree is built before clips are loaded. Alternative: infer layout from the first clip node added.

4. **Tree builder ergonomics**: the current add_child API is imperative. Would a declarative approach (describe the tree as a struct literal) be better for static trees?

5. **Should `anim.state` (state machine) be designed alongside this?** States typically own blend trees. Designing them together ensures they compose well. But `anim.state` files don't exist yet.
