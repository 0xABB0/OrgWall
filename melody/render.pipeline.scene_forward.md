# render.pipeline.scene_forward

Forward scene technique.

This is the current unified top-level scene renderer. It is not a `2d renderer` and not a `3d renderer`. It is one scene technique with multiple internal submission paths.

## Inputs

`scene_forward` consumes only canonical scene truth plus source emission:

- instances from `render.manager`
- spaces from `render.manager`
- material bindings from `render.manager`
- source-authored payload through `scene_forward_emit`

That contract is the important part. The technique should not need private knowledge of how a source stores its payload internally.

## Internal paths today

- mesh path
- sprite path

Both are internal to the same technique. They are not separate top-level pipelines anymore.

## Scene cache role

The scene cache derives execution data from canonical truth.

Today it builds:

- grouped mesh items by material base
- grouped sprite items by material base
- uploaded sprite transform/info/draw-order buffers

This cache is execution data, not canonical scene truth.

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
