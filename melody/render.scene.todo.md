# render.scene

## TODO

- The scene now owns ambient, directional lights, and point lights. Grow that toward richer probes/environment/shadow inputs without letting one technique's cache layout become scene truth.
- Directional shadow intent now exists on scene-owned lights, but it is currently only consumed by `scene_forward`. Keep the scene-side contract explicit if additional shadowing techniques arrive.
- Shadow coverage is still directional-only. Point-light shadows, cascades, and richer environment shadows need their own honest model instead of being squeezed into the current one-light path.
- Shadow authorship is now part of scene-owned directional lights, but only at the level of intent and coarse params. If more shadowing techniques arrive, keep the scene contract semantic and do not let one technique's exact matrix/atlas layout leak upward.
- When multiple scene techniques exist, keep scene-owned inputs canonical and shared. Do not let one technique's preferred cache/layout become the scene model.
