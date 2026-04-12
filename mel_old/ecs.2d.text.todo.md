# ecs.2d.text

## TODO

- `mel_ctext_atlas/sdf/msdf` default to `scale = 1.0f`. Fonts baked at different sizes produce wildly different visual sizes (28px atlas vs 128px MSDF). Should normalize to a target pixel size based on `desc->bake_size`. Something like `scale = target_size / bake_size` with a sensible default target (e.g., 24px).

- SDF glyph overlap artifacts. Overlapping glyph quads (from SDF padding) show visible rectangular backgrounds. The smoothstep discard works for isolated glyphs but adjacent glyphs' padded quads overlap. Options: tighter quad sizing (reduce padding area in UV/size), or render text to a separate target and composite.
