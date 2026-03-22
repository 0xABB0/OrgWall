# render.scene

Canonical render-scene ownership.

## Role

`render.scene` owns the scene-level render domain:

- the canonical `render.manager`
- attached render sources
- pipeline-scene caches derived from that canonical truth
- scene-owned lighting/environment input

It is the boundary between authored scene truth and per-technique execution caches.

## Contract

Sources sync into the scene's manager.

Pipelines do not invent their own scene truth. They derive from:

- instances and bindings from `render.manager`
- source emission
- scene-owned inputs such as lighting

## Lighting

Scene lighting is now authored as explicit scene-owned inputs:

- ambient color
- directional light list
- point light list

Directional lights also carry explicit shadow intent. The scene owns the light data and its shadow metadata; techniques decide how to turn that into GPU execution.

This keeps lighting truth in `render.scene` instead of hiding it inside one renderer technique or one demo.

The scene owns this data. Demos or higher-level game code author it explicitly, and techniques derive their own GPU-ready light buffers from it.

## Current limits

- this is still a forward-scene light model, not a full light/probe/environment system
- shadows are currently directional-only and consumed by `scene_forward`
- shadow maps are derived from camera-visible mesh bounds for the active view
- point-light shadows, cascades, probes, and richer environment lighting are still follow-up work
- scene composition, post/effects, and transparency resolve stay above this module
