# Melody Render Interface Proposal

This note proposes the next concrete interfaces for Melody's render chain.

It follows:

- [render.blender-comparison.md](/Users/gabbo/repo/orgwall/melody/design/render.blender-comparison.md)
- [render.stage-chain.md](/Users/gabbo/repo/orgwall/melody/design/render.stage-chain.md)

The goal is to define three things precisely:

1. view-owned response / output contract
2. scene-owned world / environment input contract
3. first explicit stage hook seam for `scene_forward`

This is an interface proposal.
It exists so we can negotiate the exact seams before pushing more renderer code.

## Design constraints

The interface must satisfy these rules:

- scene truth stays canonical
- exposure / output response are not scene-owned
- direct lights remain explicit scene truth
- environment/world input stays open
- app code can hook in deliberately
- no fixed lighting approximation becomes canonical truth
- no giant closed struct
- no fake "temporary final" solution

## What stays explicit and built-in

These scene-owned inputs are already good and should stay explicit:

- ambient color
- directional light list
- point light list
- shadow intent on lights

Why:

- they are canonical scene truth
- they are common enough to deserve direct APIs
- their ownership is clear

These should **not** be generalized away just to be abstract.

## What becomes open-ended

Two parts are still legitimately open-shaped:

1. scene-owned world/environment inputs
2. view-owned output/response inputs

These should use extensible registries instead of fixed engine structs.

## 1. Scene-owned world/environment input contract

### Purpose

This contract is for scene-authored inputs that are not direct light emitters and are not view/output policy.

Examples:

- sky material source
- environment map source
- probe set
- atmosphere input
- fog/media input
- custom game lighting contributor

### Type definition

```c
typedef struct {
    str8  name;
    usize item_size;
    usize item_align;
} Mel_Render_World_Input_Type;
```

This is a registered schema for one kind of world input.

Examples:

- `mel_render_world_input_environment_map`
- `mel_render_world_input_probe_volume`
- `mel_render_world_input_atmosphere`

### Record definition

```c
typedef struct {
    const Mel_Render_World_Input_Type* type;
    void* items;
    u32 count;
} Mel_Render_World_Input_Record;
```

### Proposed API

```c
void mel_render_scene_world_input_set(Mel_Render_Scene* scene,
                                      const Mel_Render_World_Input_Type* type,
                                      const void* items,
                                      u32 count,
                                      usize item_size);

const void* mel_render_scene_world_input_get(Mel_Render_Scene* scene,
                                             const Mel_Render_World_Input_Type* type,
                                             u32* out_count,
                                             usize* out_item_size);

void mel_render_scene_world_input_clear(Mel_Render_Scene* scene,
                                        const Mel_Render_World_Input_Type* type);
```

### Ownership and lifetime

- `scene` copies the payload
- caller retains ownership of the source memory
- returned pointer is owned by `scene`
- returned pointer stays valid until:
  - the same input type is overwritten
  - the input type is cleared
  - the scene is destroyed

### Capacity and failure rules

- `item_size` must equal `type->item_size`
- `items != nullptr` when `count > 0`
- invalid size or null misuse asserts
- allocation failure follows engine allocation/assert behavior

### Threading

- not safe to mutate concurrently with rendering
- should be authored on the same thread currently used for scene mutation
- render techniques may read during sync/draw, not write

### Why this is the right level

This keeps:

- direct lights explicit
- world/environment input open
- scene truth canonical

Without pretending we already know the final world model.

## 2. View-owned response / output contract

### Purpose

This contract is for view-specific output policy.

Examples:

- exposure
- tonemap operator
- output color transform
- debug response override
- background opacity policy

These are properties of observing/rendering the scene, not properties of the scene itself.

### Type definition

```c
typedef struct {
    str8  name;
    usize value_size;
    usize value_align;
} Mel_Render_View_Response_Type;
```

Examples:

- `mel_render_view_response_exposure`
- `mel_render_view_response_tonemap`
- `mel_render_view_response_background`

### Record definition

```c
typedef struct {
    const Mel_Render_View_Response_Type* type;
    void* value;
} Mel_Render_View_Response_Record;
```

### Proposed API

```c
void mel_render_view_response_set(Mel_Render_View_Handle view,
                                  const Mel_Render_View_Response_Type* type,
                                  const void* value,
                                  usize value_size);

const void* mel_render_view_response_get(Mel_Render_View* view,
                                         const Mel_Render_View_Response_Type* type,
                                         usize* out_value_size);

void mel_render_view_response_clear(Mel_Render_View_Handle view,
                                    const Mel_Render_View_Response_Type* type);
```

### Ownership and lifetime

- view copies the payload
- caller retains ownership of source memory
- returned pointer is owned by the view
- returned pointer stays valid until:
  - the same response type is overwritten
  - the response type is cleared
  - the view is destroyed

### Capacity and failure rules

- `value_size` must equal `type->value_size`
- invalid usage asserts
- missing response type means "no override present"

### Threading

- not safe to mutate concurrently with rendering
- authored on the same thread as other view mutations
- read by pipeline/view execution

### Why this is the right level

It lets two views of the same scene diverge cleanly:

- main camera
- minimap
- monitor-in-scene
- reflection view
- debug/editor view

Without contaminating `render.scene`.

## 3. First explicit stage hook seam for `scene_forward`

### Purpose

This is the first renderer execution hook seam.

It does **not** attempt to make every internal detail pluggable at once.
It introduces one explicit, honest mechanism for app or higher-level engine code to extend `scene_forward` at deliberate stage boundaries.

### Stage names

Initial stage names:

- `visibility`
- `shadow.prepare`
- `shadow.render`
- `lighting.prepare`
- `main.draw`
- `transparency.draw`
- `resolve`

These are string IDs, not enums.

### Hook type

```c
typedef struct {
    str8  name;
    str8  pipeline;
    str8  stage;
    i32   order;
    void  (*run)(struct Mel_Render_Stage_Ctx* ctx);
    void* user_data;
} Mel_Render_Stage_Hook_Desc;
```

### Hook handle

```c
typedef struct {
    u32 id;
} Mel_Render_Stage_Hook_Handle;
```

### Stage context

```c
typedef struct Mel_Render_Stage_Ctx {
    str8                        pipeline;
    str8                        stage;
    Mel_Render_View*            view;
    Mel_Render_Scene*           scene;
    Mel_Render_Manager*         manager;
    Mel_Render_Pipeline*        pipeline_instance;
    Mel_Render_Pipeline_Scene*  pipeline_scene;
    Mel_Render_Draw_Ctx*        draw_ctx;
    void*                       user_data;
} Mel_Render_Stage_Ctx;
```

Notes:

- `draw_ctx` may be null for non-draw stages
- pointers in the context are transient and must not be retained after the callback returns

### Stage value types

The hook seam needs a way to pass derived stage-local data between hooks and the technique.

```c
typedef struct {
    str8  name;
    usize value_size;
    usize value_align;
} Mel_Render_Stage_Value_Type;
```

### Stage value API

```c
void mel_render_stage_value_set(Mel_Render_Stage_Ctx* ctx,
                                const Mel_Render_Stage_Value_Type* type,
                                const void* value,
                                usize value_size);

const void* mel_render_stage_value_get(Mel_Render_Stage_Ctx* ctx,
                                       const Mel_Render_Stage_Value_Type* type,
                                       usize* out_value_size);

void mel_render_stage_value_clear(Mel_Render_Stage_Ctx* ctx,
                                  const Mel_Render_Stage_Value_Type* type);
```

### Hook registration API

```c
Mel_Render_Stage_Hook_Handle mel_render_stage_hook_register_opt(Mel_Render_Stage_Hook_Desc desc);
#define mel_render_stage_hook_register(...) \
    mel_render_stage_hook_register_opt((Mel_Render_Stage_Hook_Desc){__VA_ARGS__})

void mel_render_stage_hook_unregister(Mel_Render_Stage_Hook_Handle handle);
bool mel_render_stage_hook_alive(Mel_Render_Stage_Hook_Handle handle);
```

### Lifetime rules

- stage hooks are registered globally
- execution order is increasing `order`, then registration order
- stage values live only for the duration of one view render for one frame
- all stage values are cleared before the next frame for that view

### Threading

- hooks execute on the render thread for that view
- hooks for a single view/stage execute serially
- hooks must not retain `ctx` pointers after return
- hook registration/unregistration must not race active execution

### Failure rules

- invalid stage names assert in debug when a hook is executed or registered against an unknown pipeline/stage combination
- invalid type sizes assert
- hooks fail loudly by assertion if they violate contracts
- there is no partial-success protocol at this layer

### First intended use cases

#### `visibility`

- custom culling masks
- portal visibility
- software LOD decisions

#### `shadow.prepare`

- custom caster filtering
- alternate directional split selection

#### `lighting.prepare`

- inject environment approximation derived from scene world inputs
- select probes
- add atmosphere contribution

#### `resolve`

- exposure
- tonemap
- output conversion
- debug output transform

## Why this is better than a fixed `scene_environment`

Because it keeps the chain honest:

- scene truth stays in the scene
- response stays with the view
- renderer approximations stay in the renderer
- app code gets explicit extension seams

Instead of one fixed struct pretending to be all of those things at once.

## Minimal migration path from current Melody

### Keep as-is

- ambient color
- directional lights
- point lights
- scene shadow intent

### Add first

1. `Mel_Render_View_Response_Type`
2. response storage on `Mel_Render_View`
3. one built-in response type for exposure
4. one `scene_forward` stage seam:
   - `lighting.prepare` or `resolve`

### Add second

1. `Mel_Render_World_Input_Type`
2. world input storage on `Mel_Render_Scene`
3. first built-in world input type:
   - environment map or sky source reference

### Add third

- broader `scene_forward` stage hook coverage

## Recommendation

Do not continue `M3` implementation work until these three interfaces are accepted:

1. view response
2. world input
3. first stage hook seam

Once these are accepted, continue by implementing the smallest real slice:

- view-owned exposure response
- `resolve` stage hook

That is the first clean step that moves the engine forward without hardcoding the wrong model.
