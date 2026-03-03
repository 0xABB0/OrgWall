# Font Architecture

## Status

This document is target architecture (vNext).
Only `font.atlas.*` is currently implemented.

→ General asset loading patterns: `engine.assets.md`

---

## Shared: Mel_Font_Descriptor

Pure metadata. Every font module produces this. Every consumer reads this.

```c
struct Mel_Font_Glyph {
    f32 x0, y0, x1, y1;        // bounding box in pixels
    f32 u0, v0, u1, v1;        // UV coordinates in atlas/texture
    f32 xadvance;               // horizontal advance
};

struct Mel_Font_Descriptor {
    Mel_Font_Glyph* glyphs;
    u32 glyph_count;
    u32 first_codepoint;        // typically 32 (space)
    f32 line_height;
    f32 ascent;
};
```

---

## Three Rendering Modules (Target vNext)

Each is its own pool (module static), its own handle type, its own GPU data:

**`font.atlas`** — pre-rendered bitmap atlas.
Cheap, pixel-perfect at target size. Uses stb_truetype to rasterize glyphs into a
texture atlas. Fastest to load, lowest quality at non-native sizes.

```c
Mel_Font_Atlas_Handle mel_load_font_atlas(str8 path, .size = 20.0f);
```

**`font.sdf`** — signed distance field.
Resolution-independent. Single-channel distance texture. Good quality scaling,
slight rounding at sharp corners.

```c
Mel_Font_SDF_Handle mel_load_font_sdf(str8 path, .size = 48.0f);
```

**`font.msdf`** — multi-channel signed distance field.
Sharp corners preserved. Best quality/perf tradeoff. Requires MSDF-specific shader.

```c
Mel_Font_MSDF_Handle mel_load_font_msdf(str8 path, .size = 48.0f);
```

---

## Adding A New Font Technique

1. Create `font.X.h` / `font.X.c`
2. Define `Mel_Font_X_Pool`, `Mel_Font_X_Handle`, `Mel_Font_X_Entry`
3. Entry contains `Mel_Font_Descriptor` + X-specific GPU data
4. Implement `mel_load_font_X(str8 path, ...)` with `_opt` pattern
5. Pool is a static in `font.X.c`, initialized by `mel_init()`

Nothing else changes. No central registry, no type dispatch.
The rendering pipeline binds the right shader based on which font handle type is used.

---

## Open Questions

1. **Font descriptor expansion**: current descriptor is ASCII-only (96 glyphs).
   Unicode support needs glyph-on-demand or pre-built ranges. Design separately.
