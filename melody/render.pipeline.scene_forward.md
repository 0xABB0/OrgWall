# render.pipeline.scene_forward

Forward scene technique.

This is the current unified top-level scene renderer. It is not a `2d renderer` and not a `3d renderer`. It is one scene technique with multiple internal submission paths.

## Inputs

`scene_forward` consumes only canonical scene truth plus source emission:

- instances from `render.manager`
- spaces from `render.manager`
- material bindings from `render.manager`
- source-authored payload through `scene_forward_emit`
- scene-owned lighting from `render.scene`

That contract is the important part. The technique should not need private knowledge of how a source stores its payload internally.

## Internal paths today

- mesh path
- sprite path

The mesh path also has a directional shadow pass. The scene authors shadow intent on directional lights, and `scene_forward` consumes that intent to build exactly one shadow map for the first shadow-casting directional light.

The shadow map fit is derived from the camera-visible mesh bounds for the active view. That keeps the pass tied to the scene the camera is actually seeing, without turning bounds fitting into canonical scene truth.

The mesh path now has explicit opaque and alpha-blend submission phases. Blend state comes from material-instance truth, not from the scene model.

Its lighting path now consumes:

- ambient scene color
- a scene-owned directional light list
- a scene-owned point light list

and uploads compact per-view light buffers for the shader to consume. That keeps light truth in the scene while keeping execution data in the technique.

Both are internal to the same technique. They are not separate top-level pipelines anymore.

## Scene cache role

The scene cache derives execution data from canonical truth.

Today it builds:

- grouped mesh items by material base
- grouped sprite items by material base
- uploaded sprite transform/info/draw-order buffers

This cache is execution data, not canonical scene truth.

## Visibility today

The mesh path now performs basic scene-scale CPU frustum culling at draw time.

It uses:

- the active view frustum
- item bounds transformed by item model
- item hidden flags
- the view visibility mask against item layer masks

This keeps culling in derived execution, where it belongs, instead of polluting canonical scene truth.

## Source emitter contract

Sources can emit:

- `Mel_Scene_Forward_Sprite`
- `Mel_Scene_Forward_Mesh`

Each emitted item selects a `material_binding_index`. The technique resolves that index through the instance material bindings and places the item into the correct material-base range.

That is what enables:

- one instance, many material bindings
- one source, many emitted parts
- each part using a different material

## Reference validation

The current reference example is:

- [example.multimaterial_mesh.c](/Users/gabbo/repo/orgwall/melody/examples/example.multimaterial_mesh.c)

Expected result:

- one rotating object
- two mesh parts
- left part red
- right part blue

If both parts render with the same material, the binding-selection path is wrong.

## Current limits

- mesh path still assumes whole-geometry handles, not explicit submesh index ranges inside one geometry handle
- there is no general scene-composition/effects layer here
- this technique is still one renderer strategy, not the whole rendering architecture
- lighting is now scene-owned and no longer single-light, but it is still a basic forward model: ambient plus directional and point lights
- shadowing is currently directional-only and limited to one shadow-casting directional light per `scene_forward` view
- point-light shadows and cascades are not implemented yet
- the shadow map is fitted from camera-visible mesh bounds, not from a scene-wide light clustering or cascade system
- alpha-blend mesh sorting is still basic: back-to-front by item bounds center
- culling is currently CPU frustum culling in the draw path, not a richer scene-wide GPU/async visibility system
