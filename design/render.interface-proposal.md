# Melody Render Interface Proposal

This note replaces the earlier overly-generic proposal.

It is based on:

- [render.blender-comparison.md](/Users/gabbo/repo/orgwall/melody/design/render.blender-comparison.md)
- [render.stage-chain.md](/Users/gabbo/repo/orgwall/melody/design/render.stage-chain.md)

The previous proposal had three big problems:

1. it tried to generalize world input too early
2. it tried to generalize stage hooks too early
3. it was drifting toward a generic event bus / blob-registry design

That is not aligned with the engine spirit.

This proposal is intentionally narrower.

## Core position

Do **not** build:

- a generic world-input registry
- a generic stage-hook registry
- a generic public stage-value registry

yet.

Instead:

1. keep canonical scene truth explicit
2. keep view/output response explicit
3. expose only one real extensibility seam now:
   - the per-view response / resolve chain

When another stage becomes real and justified, give it its own dedicated contract.

No speculative framework first.

## Ownership split

### Scene-owned truth

Scene-owned render truth currently includes:

- ambient color
- directional lights
- point lights
- shadow intent on lights
- instances / source bindings / material bindings in the scene DB

This stays explicit.

Do **not** add to `render.scene` yet:

- exposure
- tonemap
- fixed sky/ground approximations
- generic environment blobs

Future scene/world systems should arrive as explicit engine modules when they become real:

- `render.environment`
- `render.probe`
- `render.atmosphere`
- `render.fog`

or similarly scoped modules.

The key rule is:

- scene truth gets explicit APIs when the concept is canonical
- we do not hide uncertainty behind a generic blob registry

### View-owned truth

View-owned truth should include:

- camera
- target
- visibility mask
- output response policy

This is the correct home for:

- exposure
- tonemap
- output transform
- response/debug overrides

Why:

- multiple views of the same scene must be allowed to diverge
- this matches Blender's world-vs-film split
- this matches how serious engines tend to separate scene inputs from output response

### Technique-owned truth

Technique-owned truth remains:

- visibility structures
- shadow data
- intermediate render targets
- clustered light data
- draw generation data
- transparency queues
- per-view persistent response state

This data is derived.
It is not canonical scene or view truth.

## What we expose first

The first explicit extension subsystem should be:

- per-view response / resolve

Not:

- visibility replacement
- shadow replacement
- generic lighting-preparation hooks

Why:

- exposure/tonemap/output response clearly belong on the view side
- the seam is conceptually clean
- it is useful immediately
- it does not require us to invent fake generality for all stages

It is still mechanically expensive.
But it is the first honest seam.

## View response chain

This is the one subsystem we should add next.

### Concept

A view owns an ordered response chain.

Each response operation consumes an input image and produces an output image for the next step.

Typical uses:

- exposure
- tonemap
- output transform
- debug response visualization

This is **not** a generic stage-hook system.
It is a dedicated subsystem for the resolve/output stage.

### Public type

```c
typedef struct Mel_Render_Response_Op_Type {
    str8  name;
    usize params_size;
    usize params_align;
    void  (*run)(struct Mel_Render_Response_Ctx* ctx, const void* params);
} Mel_Render_Response_Op_Type;
```

This is a response operation type.

Examples:

- `mel_render_response_exposure_manual`
- `mel_render_response_tonemap_aces`
- `mel_render_response_output_srgb`

### Response op instance

```c
typedef struct {
    const Mel_Render_Response_Op_Type* type;
    void* params;
    i32 order;
} Mel_Render_Response_Op;
```

### Response op handle

```c
typedef struct {
    u32 id;
} Mel_Render_View_Response_Op_Handle;
```

### Response context

```c
typedef struct Mel_Render_Response_Ctx {
    Mel_Render_View*     view;
    Mel_Render_Scene*    scene;
    Mel_Render_Draw_Ctx* draw_ctx;
    Mel_Gpu_Image*       src;
    u32                  src_texture_idx;
    Mel_Render_Target*   dst_target;
    Mel_Gpu_Image*       dst;
    void*                user_data;
} Mel_Render_Response_Ctx;
```

Important:

- `src` is the rendered HDR source image for this resolve step
- `src_texture_idx` is the ready-to-sample source binding for `src`
- `dst_target` is the render target for this step
- `dst` is the underlying image when `dst_target` is offscreen, otherwise `nullptr`
- if an op needs the rendered frame, it gets it here explicitly
- this is not "leaking internals"; this is the documented contract of the resolve subsystem

A raw `Mel_Gpu_Image*` alone is not enough for a useful custom response op.
The context must carry:

- whatever ready-to-sample handle the engine uses for source-image sampling
- and the actual render target that the op is expected to render into

This matters because the final resolve destination may be a swapchain target, not an offscreen image.

### Public API

```c
typedef struct {
    Mel_Render_View_Handle view;
    const Mel_Render_Response_Op_Type* type;
    const void* params;
    usize params_size;
    i32 order;
    void* user_data;
} Mel_Render_View_Response_Op_Desc;

Mel_Render_View_Response_Op_Handle
mel_render_view_response_op_add_impl(Mel_Render_View_Response_Op_Desc desc);

#define mel_render_view_response_op_add(view, type, params_ptr, ...) \
    mel_render_view_response_op_add_impl((Mel_Render_View_Response_Op_Desc){ \
        .view = (view), \
        .type = (type), \
        .params = (params_ptr), \
        .params_size = sizeof(*(params_ptr)), \
        __VA_ARGS__ \
    })

void mel_render_view_response_op_clear(Mel_Render_View_Handle view,
                                       const Mel_Render_Response_Op_Type* type);

void mel_render_view_response_ops_clear_all(Mel_Render_View_Handle view);

void mel_render_view_response_op_remove(Mel_Render_View_Response_Op_Handle handle);
bool mel_render_view_response_op_alive(Mel_Render_View_Response_Op_Handle handle);
```

### Ordering

Response ops execute in:

1. increasing `order`
2. registration order as the tie-breaker

### Ownership and lifetime

- the view copies response-op params
- op params are view-owned
- response-op registration is per-view
- response ops are destroyed automatically when the view is destroyed
- `user_data` is not copied
- `user_data` remains caller-owned and must remain valid while the op is registered

### Dynamic params

Response-op params are copied at add-time.

That makes them appropriate for static configuration.

If a value must change every frame:

- either remove and re-add the op
- or point `user_data` at live app state and read that in `run(...)`

The engine should not pretend copied params are a live mutable binding.

### Alignment guarantee

Response-op param storage must honor `type->params_align`.

This is a hard guarantee.

### Failure rules

- misuse asserts in debug
- size mismatch asserts
- null params for non-zero-sized types assert
- there is no recoverable failure return path here

### Threading

- view response mutation is not concurrent-safe with rendering
- mutate on the same thread used for other view mutations
- execution happens on the render thread for that view

## First built-in response type

The first built-in response op should be manual exposure only.

```c
typedef struct {
    f32 value;
} Mel_Render_Response_Exposure_Manual_Params;
```

This keeps the first step honest.

It does **not** pretend to solve auto-exposure.

If auto-exposure is added later, it should be a separate response op type with:

- view-authored params
- technique-owned temporal adaptation state

That split is important:

- authored control stays on the view
- temporal feedback state stays in the technique's per-view instance data

## scene_forward integration

### What changes

To support a real response chain, `scene_forward` needs:

- an intermediate HDR color target
- a resolve pass
- a resolve shader/pipeline or direct op-dispatch path
- mesh and sprite output redirected into the intermediate instead of the final target
- ping-pong intermediates when more than one response op is active

Example chains:

- `0` ops: direct write to final target
- `1` op: HDR intermediate -> final target
- `2` ops: HDR intermediate -> ping -> final target
- `3` ops: HDR intermediate -> ping -> pong -> final target

These ping-pong intermediates are technique-owned execution resources.
The response subsystem does not own them.

This is not a tiny refactor.

### Resolve elision rule

`scene_forward` must not always pay this cost.

If all of the following are true:

- the view has no response ops
- `scene_forward` has no built-in resolve work active for that view

then `scene_forward` may bypass the intermediate HDR target and write directly to the final target.

This is mandatory.

Otherwise we would violate:

- no hidden GPU memory cost
- no unnecessary fullscreen pass
- no paying for what is not used

### Why resolve is still the first seam

Because even though it is expensive mechanically, it is the cleanest architectural seam:

- view-owned response belongs here
- it does not require exposing deeper technique internals
- it does not require pretending visibility/shadow replacement is cheap

## What we are deliberately *not* exposing yet

### Not yet: world-input registry

Why not:

- we still do not know the correct canonical world/environment model
- a registry here would be abstraction before understanding

When a real environment system arrives, define it explicitly.

### Not yet: generic stage hooks

Why not:

- too easy to become a shadow second renderer
- too easy to create undocumented cooperative protocols
- current `scene_forward` execution structure does not have clean boundaries for this yet

### Not yet: visibility replacement

Why not:

- current draw/shadow code computes culling inline
- a hook would be fake unless draw/shadow stages become consumers of produced visibility data
- that is a much deeper structural refactor

## Future stage exposure rule

When a deeper stage becomes worth exposing, do **not** reuse a generic hook system.

Instead:

1. refactor that stage into a real sub-phase
2. define the exact stable input/output contract for that one stage
3. add a dedicated stage-specific API
4. document producer/consumer behavior explicitly

Examples of future dedicated APIs:

- `scene_forward.visibility`
- `scene_forward.shadow_setup`
- `scene_forward.light_selection`

But only when those stages are structurally real.

## Why this is better

Because it removes the fake generality.

It gives us:

- explicit scene truth
- explicit view response
- one real extension subsystem

Without:

- a generic event bus
- a public stage-value blob registry
- speculative world/environment registries
- pretending all stages are already hookable

## Remaining open problems

These are still real and should remain explicit.

### 1. Resolve is still mechanically expensive

Architecturally clean does not mean cheap to implement.

### 2. Response op model may still be too generic

This is much narrower than the earlier proposal, but it is still a mini framework.
That is acceptable only if we keep the first implementation tight.

### 3. Some response controls still need sharper ownership

Examples:

- background opacity
- debug output transforms
- certain presentation/output policies

The direction is "view/output, not scene," but some exact splits with technique ownership remain open.

### 4. Future environment systems still need real design

We explicitly chose not to solve that with a fake registry.
That means the actual environment/world-input model still needs to be designed when the concrete feature arrives.

## Recommendation

Do not implement a generic world-input or generic stage-hook system now.

Implement only:

1. per-view response chain
2. first built-in response op: manual exposure
3. `scene_forward` resolve integration with resolve elision

That is the first slice that is:

- forward-facing
- extensible
- aligned with other serious engines
- honest about cost
