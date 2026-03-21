# render.manager

Canonical render-scene database.

The manager owns render instance truth. It does not own source payload. It does not own packed execution buffers. It gives the renderer stable handles, compact storage, and canonical bindings that techniques can derive from.

## What lives here

- `Mel_Render_Handle`: stable instance handle with generation
- `Mel_Render_Instance`: scene-level binding record
- `Mel_Render_Material_Binding`: per-instance material bindings
- `Mel_Render_Space_Handle`: scene-level space binding
- typed space payload storage and lifetime
- dirty tracking and mutation serial

## What does not live here

- sprite payload
- mesh payload
- text/glyph payload
- packed draw lists
- GPU-ready execution caches
- post/effect resources

Those belong to sources or technique caches.

## Current instance model

An instance currently binds:

- `source`
- `space`
- `flags`
- `visibility_mask`
- material-binding count

Material bindings are sidecar storage keyed by instance handle. This keeps the core instance compact and allows one instance to bind multiple materials without baking fixed slots into the struct.

Spaces are also sidecar storage keyed by space handle. A space owns typed payload for spatial state. The current engine uses this for:

- manual 3D model matrices
- ECS 2D sprite transforms

The important rule is:

`instance` says which space is used.
`space payload` says what the spatial data is.

## Design rules

- canonical truth only
- handle-indexed ownership
- swap-on-free packed storage
- only allocate sidecars when used
- no closed object union
- no technique-shaped canonical storage

## Why this shape

This module exists so scenes can express:

- one instance, many material bindings
- one instance, explicit space binding
- one source reused by many instances
- sources with very different payload shapes

without forcing the manager to know every renderable payload layout in advance.

## Current limits

- spaces are typed, but still very lightweight
- there is no higher-level scene-global light/world model here yet
- material bindings are per-instance, but source/technique support for using many bindings is still growing
