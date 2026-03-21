# render.source.manual

Manual authored render source.

This source is the simplest direct way to put render instances into a scene without ECS. It owns authored payload. The manager only stores the instance bindings.

## What it owns

- mesh-part payload per handle
- bounds per handle
- pending adds/removes before sync

It does not own:

- instance handles
- material bindings in the manager
- space payload lifetime in the manager

## Current authored model

Per manual object:

- one instance handle
- one space handle
- bounds
- one or more mesh parts

Each mesh part currently contains:

- `mesh`
- `material_binding_index`
- `flags`

This is enough to express:

- one object made of many sub-parts
- each part selecting a different material binding

The reference example is:

- [example.multimaterial_mesh.c](/Users/gabbo/repo/orgwall/melody/examples/example.multimaterial_mesh.c)

## Sync contract

On sync, the source writes:

- the instance record
- material bindings for that instance

The source keeps its authored payload locally and later emits it to `scene_forward` through the source-type callback.

## Public usage

Typical flow:

```c
Mel_Render_Handle h = mel_source_manual_add(source, model, bounds, info);
mel_source_manual_set_material_bindings(source, h, bindings, binding_count);
mel_source_manual_set_mesh_parts(source, h, parts, part_count);
mel_source_manual_set_transform(source, h, new_model);
```

`mel_source_manual_add(...)` still accepts `Mel_Render_Info` for the common one-mesh / one-binding case. The richer APIs layer on top when needed.

## Design intent

This module is the reference source for:

- explicit instance ownership
- explicit space binding
- multi-part emission
- multi-material instance binding

If a more complex source cannot express itself as cleanly as this one, the core model is probably still wrong.
