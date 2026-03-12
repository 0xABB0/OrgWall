# Engine Ownership Refactor — SUPERSEDED

This document has been split into focused design docs for clarity:

- **`engine.overview.md`** — architecture map, module statics + `_opt`, state tiers
- **`engine.app.md`** — app entry (current + target), lifecycle, init/shutdown ordering
- **`engine.sim.md`** — simulation identity, registration, time scaling, event lifecycle
- **`engine.frame.md`** — frame pipeline, fixed update contexts, variable updates, deferred mutations
- **`engine.render.md`** — render lists, producers, ECS sync, draw API
- **`engine.source.md`** — render sources as typed inputs and intermediates beyond just lists
- **`engine.view.md`** — views as the bridge between cameras, lists, windows, and graph execution
- **`engine.frame.recipe.md`** — frame planning that binds views, techniques, and swapchains into graph topology
- **`engine.technique.md`** — rendering techniques as first-class execution strategies and native engine modules
- **`engine.render.graph.md`** — render graph, passes, GPU-driven, bindless, materials
- **`engine.assets.md`** — per-type pools, async loading
- **`engine.fonts.md`** — font descriptor, atlas/SDF/MSDF rendering modules
- **`engine.async.md`** — job system, IO system, frame arena, threading

Start with `engine.overview.md`.
