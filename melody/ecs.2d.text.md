# ecs.2d.text

ECS text component and helper functions for rendering text through the 2D pipeline.

## Mel_CText

```c
typedef struct Mel_CText {
    str8 text;
    Mel_Font_Descriptor* desc;
    u32 texture_idx;
    Mel_Material_Base_Id material_id;
    Mel_Material_Instance_Id material_instance;
    Mel_Vec4 color;
    f32 scale;
} Mel_CText;
```

Technique-agnostic. The `material_id` determines which shader (atlas/SDF/MSDF). The `desc` provides glyph metrics for layout. The `texture_idx` is the font texture's index in the bindless texture table. The `material_instance` holds style params (color, edge, softness, outline) for SDF/MSDF techniques.

## Helpers

```c
Mel_CText mel_ctext_atlas(Mel_Font_Atlas_Handle font, str8 text, Mel_Vec4 color);
Mel_CText mel_ctext_sdf(Mel_Font_SDF_Handle font, str8 text, Mel_Vec4 color);
Mel_CText mel_ctext_msdf(Mel_Font_MSDF_Handle font, str8 text, Mel_Vec4 color);
```

Each helper: extracts the font descriptor and texture from the technique-specific handle, registers the texture in the bindless table (deduplicated), allocates a material instance with sensible defaults, returns a ready-to-use component.

## Source Sync

The ECS 2D source (`render.source.ecs.2d.c`) observes `{Mel_CTransform, Mel_CText}` via a second delta tracker. On add/modify: expands the text string into N glyph quads in the manager. Each glyph is a separate manager entry with:
- Transform: position computed from glyph metrics (xadvance accumulation, line_height for newlines), scaled by `Mel_CText.scale`
- Info: UV from font descriptor, color, texture_idx, material_base_id, material_instance as layer

One text entity → N manager handles, tracked in a `Mel_Text_Glyph_Block`. On remove: all glyph handles freed. On modify: old glyphs freed, re-expanded.

The text delta is guarded on `ecs_id(Mel_CText) != 0` — if the text component isn't registered with flecs, the delta is skipped. This allows the sprite-only examples to work without registering `Mel_CText`.
