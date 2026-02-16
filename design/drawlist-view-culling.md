# Draw List + View + Culling System

## Problem

Currently `mel_game_draw()` directly queries ECS and submits to sprite batch — tight coupling. The rendering architecture needs an intermediate layer that:

- Decouples ECS data from rendering submission
- Supports multiple cameras/views seeing the same world independently
- Enables per-view culling, LOD, and dead-pass elimination
- Provides persistent, GPU-friendly data layout (contiguous packed arrays)

## Constraints

- Draw lists are per-pipeline per-view, containing material/instance data
- Slotmap-backed for persistent slots, contiguous packed data for iteration and future GPU upload
- Views represent camera perspectives (projection + viewport). Each view creates and owns its draw lists. Multiple views see the same world independently (main camera + minimap, splitscreen, etc.)
- Flecs observers handle lifecycle: OnAdd -> allocate slot in draw list, OnRemove -> free slot, OnSet -> re-sync dirty data. One observer per (view, pipeline) pair
- Systems handle LOD, frustum culling, occlusion culling, imposters — operating on draw list entries
- Render graph handles dead-pass elimination. Example: 3D world with a monitor showing a 2D game — if the monitor is culled, the entire 2D pass is skipped because nothing reads its render target
- Progressive disclosure: Layer 0 (just set components, it draws) -> Layer 1 (configure defaults) -> Layer 2 (add passes) -> Layer 3 (full custom graph). Engine eats its own dogfood
- Projection is on the view, draw lists are data. Completely separate concerns composed at the render site

## Open Questions

- Should `Mel_Draw_List` store its `item_size` or should the slotmap handle that transparently?
- The `_opt` pattern: should `mel_draw_list_init` take allocator as a separate param or inside the opt struct? Current plan has it separate, but other modules (render.graph) put it in the opt.
- `Mel_Sprite_Instance` lives inside `render.drawlist.*` — is that the right home? It's sprite-specific but tightly coupled to the draw list flush path. Alternative: `sprite.instance.h` or keep it where the pipeline-specific flush logic lives.
- The `bool* visible` array parallel to packed data requires careful bookkeeping on swap-remove. Alternative: use `Mel_BitSet` instead of raw `bool*`? Pro: memory-efficient. Con: more indirection, bitwise ops on hot path.
- Observer registration happens in demo/game code using raw flecs API. Should there be helper functions like `mel_draw_list_observe_sprites(list, world)` for common patterns, or is that Layer 0 progressive disclosure material for later?
- `mel_render_view_create_draw_list` returns a pointer into the view's dynamic array — that pointer invalidates if more draw lists are added. Should we return an index instead? Or guarantee no realloc after first frame?

## Discussion

### Architecture overview

```
ECS World
  |
  | (flecs observers: OnAdd, OnRemove, OnSet)
  | (one observer per (view, pipeline) pair)
  v
Draw Lists (slotmap-backed, per-pipeline per-view)
  |
  | (systems: frustum culling, LOD, occlusion)
  v
Visibility flags (parallel bool array)
  |
  | (flush: iterate packed data, skip !visible)
  v
Sprite Batch / Mesh Batch / etc.
  |
  v
GPU
```

### Draw list — `melody/render.drawlist.*`

**`render.drawlist.fwd.h`**
```c
typedef struct Mel_Draw_List Mel_Draw_List;
typedef void (*Mel_Draw_Sync_Fn)(ecs_world_t* world, ecs_entity_t e, void* slot_data);
```

**`render.drawlist.h`** — includes: `render.drawlist.fwd.h`, `collection.slotmap.fwd.h`, `collection.hashmap.fwd.h`, `allocator.fwd.h`, `sprite.batch.fwd.h`, `gpu.texture.fwd.h`, `math.vec4.h`, `math.geo.rect.h`

```c
struct Mel_Draw_List {
    Mel_SlotMap slots;
    Mel_HashMap entity_map;   // entity id (u64) -> handle value (u32, as void*)
    bool* visible;            // parallel to packed data
    u32 visible_capacity;
    const Mel_Alloc* alloc;
};
```

**API:**
```c
void mel_draw_list_init_opt(Mel_Draw_List* list, const Mel_Alloc* alloc, Mel_Draw_List_Opt);
#define mel_draw_list_init(list, alloc, ...) ...
void  mel_draw_list_destroy(Mel_Draw_List* list);

Mel_SlotMap_Handle mel_draw_list_add(Mel_Draw_List* list, u64 entity_id);
void  mel_draw_list_remove(Mel_Draw_List* list, u64 entity_id);
void* mel_draw_list_get(Mel_Draw_List* list, u64 entity_id);
bool  mel_draw_list_has(Mel_Draw_List* list, u64 entity_id);

u32   mel_draw_list_count(Mel_Draw_List* list);
void* mel_draw_list_data(Mel_Draw_List* list);

void  mel_draw_list_set_visible(Mel_Draw_List* list, u64 entity_id, bool vis);
void  mel_draw_list_set_all_visible(Mel_Draw_List* list);
void  mel_draw_list_set_all_hidden(Mel_Draw_List* list);
```

**Sprite instance layout** (first built-in type):
```c
typedef struct {
    f32 x, y, w, h;
    f32 u0, v0, u1, v1;
    f32 r, g, b, a;
    u32 texture_index;
} Mel_Sprite_Instance;

void mel_sync_sprite_2d(ecs_world_t* world, ecs_entity_t e, void* slot_data);
void mel_draw_list_flush_sprites(Mel_Draw_List* list, Mel_SpriteBatch* batch, Mel_Gpu_Texture* default_texture);
```

**2D culling** (operates on sprite instance layout):
```c
void mel_cull_2d_rect(Mel_Draw_List* list, Mel_Rect visible_area);
```
Iterates packed `Mel_Sprite_Instance` data, tests each sprite rect against `visible_area` using `mel_rect_overlaps`, sets `visible[i]` accordingly.

**Implementation notes:**
- `add`: allocates a zeroed slot in slotmap, stores entity->handle in hashmap, grows visible array if needed, sets `visible[packed_idx] = true`
- `remove`: looks up handle from hashmap, reads packed_idx BEFORE remove, calls `mel_slotmap_remove` (which may swap-move the last element), swaps visible flags to match, removes from hashmap
- `flush_sprites`: iterates packed `Mel_Sprite_Instance` data, skips where `visible[i] == false`, calls `mel_sprite_batch_draw_uv` per visible entry
- `mel_sync_sprite_2d`: reads CTransform + Sprite from entity, writes to Mel_Sprite_Instance slot
- `mel_cull_2d_rect`: for each packed entry, builds `mel_rect(inst->x, inst->y, inst->w, inst->h)`, checks `mel_rect_overlaps` against visible_area

### View — `melody/render.view.*`

**`render.view.fwd.h`**
```c
typedef struct Mel_Render_View Mel_Render_View;
```

**`render.view.h`** — includes: `render.view.fwd.h`, `render.drawlist.fwd.h`, `collection.array.fwd.h`, `math.mat4.h`, `math.geo.rect.h`, `allocator.fwd.h`

```c
struct Mel_Render_View {
    Mel_Mat4 projection;
    Mel_Rect viewport;
    Mel_Array(Mel_Draw_List) draw_lists;
    const Mel_Alloc* alloc;
};
```

**API:**
```c
void mel_render_view_init(Mel_Render_View* view, const Mel_Alloc* alloc);
void mel_render_view_destroy(Mel_Render_View* view);

void mel_render_view_set_ortho(Mel_Render_View* view, f32 left, f32 right, f32 bottom, f32 top);
void mel_render_view_set_viewport(Mel_Render_View* view, Mel_Rect viewport);

Mel_Draw_List* mel_render_view_create_draw_list_opt(Mel_Render_View* view, Mel_Draw_List_Opt);
#define mel_render_view_create_draw_list(view, ...) ...
```

The view creates draw lists internally (`mel_array_push` of `Mel_Draw_List` value). Returns pointer to the created draw list for external use (observer registration, sync, flush).

`set_ortho` stores both the projection matrix and the world-space viewport rect (the ortho bounds = left,bottom,right-left,top-bottom).

### Additional forward declarations needed

**`melody/sprite.batch.fwd.h`**
```c
typedef struct Mel_SpriteBatch Mel_SpriteBatch;
```

**`melody/collection.hashmap.fwd.h`** (check if exists first)
```c
typedef struct Mel_HashMap Mel_HashMap;
```

### Demo — `demos/demo.drawlist.c`

Shows two views (main + minimap) with per-view frustum culling.

**Setup:**
```c
Mel_Render_View main_view, minimap_view;
mel_render_view_init(&main_view, alloc);
mel_render_view_init(&minimap_view, alloc);

mel_render_view_set_ortho(&main_view, 0, 640, 0, 480);
mel_render_view_set_ortho(&minimap_view, -200, 840, -200, 680);  // zoomed out

Mel_Draw_List* main_sprites = mel_render_view_create_draw_list(&main_view,
    .item_size = sizeof(Mel_Sprite_Instance));
Mel_Draw_List* mini_sprites = mel_render_view_create_draw_list(&minimap_view,
    .item_size = sizeof(Mel_Sprite_Instance));
```

**Observer registration** (using flecs API directly):
```c
ecs_observer(world, {
    .events = { EcsOnAdd },
    .query.terms = {
        { .id = ecs_id(Mel_CTransform) },
        { .id = ecs_id(Mel_Sprite) },
    },
    .callback = on_sprite_add,
    .ctx = main_sprites,
});
// same for mini_sprites, and OnRemove/OnSet observers for each
```

The observer callbacks use `mel_draw_list_add/remove/get` + `mel_sync_sprite_2d`.

**Per-frame:**
```c
mel_ecs_update(&ecs, dt);

mel_cull_2d_rect(main_sprites, main_view.viewport);
mel_cull_2d_rect(mini_sprites, minimap_view.viewport);

// render main view (full screen)
mel_sprite_batch_begin(&batch, &pipeline);
mel_draw_list_flush_sprites(main_sprites, &batch, &white_texture);
mel_sprite_batch_end(&batch, &dev, cmd, &main_view.projection);

// render minimap (small rect in corner)
mel_sprite_batch_begin(&batch, &pipeline);
mel_draw_list_flush_sprites(mini_sprites, &batch, &white_texture);
mel_sprite_batch_end(&batch, &dev, cmd, &minimap_view.projection);
```

**Movement system**: wraps entities at world bounds. WASD to pan the main view's camera (shifts the ortho projection). Entities going off-screen in the main view get culled from that view but remain visible in the minimap.

## How This Extends

**Multi-world (3D monitor example):**
- World_2D has a view -> draws to a render target texture
- World_3D has a view -> the monitor mesh uses render target as texture
- If monitor is culled in World_3D's view, render graph sees no consumer for the render target -> skips World_2D's pass entirely
- Implementation needs: render target abstraction, render graph resource tracking, cross-world texture references

**LOD:**
- Same entity has multiple representations (high-poly mesh, low-poly mesh, imposter billboard)
- A LOD system runs per-view: checks distance from entity to camera, picks representation
- Each representation type has its own draw list and pipeline
- The system adds/removes the entity from the correct draw list based on distance
- Implementation needs: distance-to-camera calculation, multiple draw lists per entity (via flecs pairs for handle storage)

**Occlusion culling:**
- After frustum culling, a hierarchical Z-buffer or occlusion query pass runs
- Results feed back into the draw list visibility
- Dead-pass elimination in render graph prunes unused passes
- Implementation needs: depth pre-pass, occlusion query support, render graph liveness analysis

**Progressive disclosure (future):**
- Layer 0: Just set Mel_CTransform + Mel_Sprite on an entity, it draws. Engine auto-creates default view + draw list + observers.
- Layer 1: Configure the default view (change projection, viewport).
- Layer 2: Add your own views, draw lists, observers.
- Layer 3: Full custom render graph with your own passes, resources, culling.

## Decision Log

- **Draw list backing**: Slotmap, not per-frame array. Persistent slots, generational handles, contiguous packed data. (Decided during architecture discussion)
- **Entity->handle mapping**: HashMap inside draw list, not ECS component. Avoids polluting ECS with rendering bookkeeping, works cleanly with multi-view (same entity in N draw lists). (Decided during architecture discussion)
- **View owns draw lists**: View creates them via `mel_render_view_create_draw_list()`. Not standalone draw lists that reference a view. (Decided after discussing ownership)
- **Projection on view, not draw list**: Draw lists are pure data. The view carries the camera perspective. Composed at render site. (Decided during architecture discussion)
- **Observers, not per-frame queries**: Flecs observers for lifecycle (add/remove/set). One observer per (view, pipeline) pair. Not the current pattern of querying every frame. (Decided during architecture discussion)

## Existing Code Reused

- `Mel_SlotMap` — `melody/collection.slotmap.h` (generational handle storage)
- `Mel_HashMap` — `melody/collection.hashmap.h` (entity->handle mapping, `mel_hashmap_hash_u64`/`mel_hashmap_eq_u64`)
- `Mel_Array` — `melody/collection.array.fwd.h` (view's draw list collection)
- `Mel_Rect` / `mel_rect_overlaps` — `melody/math.geo.rect.h` (viewport + culling)
- `mel_mat4_ortho` — `melody/math.mat4.h`
- `mel_sprite_batch_draw_uv` — `melody/sprite.batch.h:62`
- `ecs_observer()` — flecs API for lifecycle events
- `ecs_get()` — flecs component access in sync
- Demo pattern — `demos/demo.breakout.c`

## Scope — This Implementation

- Draw list (slotmap + entity->handle hashmap + per-entry visibility)
- View (projection, viewport, owns draw lists, creates them)
- 2D frustum culling function on draw lists
- Sprite instance type + flush + sync function
- Demo with two views (main + minimap) showing per-view culling

## Explicitly Deferred

- GPU-backed draw lists
- Bindless sync path
- VisibleBy(camera) flecs relationships
- Render graph integration / dead-pass elimination
- Progressive disclosure auto-setup
- 3D / mesh support
- Multi-world render targets
