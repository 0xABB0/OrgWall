# Font Architecture

## Status

This document is target architecture (vNext).
Current implementation has atlas, SDF, and MSDF working but with manual pool management and no descriptor/loader split.

---

## Prerequisites

Things missing from the engine (outside the font system) that must exist before this architecture can be fully implemented.

### `Mel_Gpu_Texture_Opt.format`

`gpu.texture.h` currently has no format field. `gpu.texture.c` hardcodes SRGB. SDF and MSDF textures MUST use UNORM (distance field values are linear data, not color — sRGB gamma-decodes them and breaks the math). Without a format field, every SDF/MSDF technique does ~30 lines of manual staging buffer + image init + sampler creation to bypass the texture helper. Adding `.format` to the opt struct (defaulting to SRGB for backwards compat) collapses all of that into `mel_gpu_texture_init(tex, dev, .pixels = data, .width = w, .height = h, .format = MEL_FORMAT_RGBA8_UNORM)`.

This is one field and a few lines in `gpu.texture.c`. Small change, massive quality-of-life for any non-sRGB texture.

### Constructor Priority Slots and Startup Counter

See `design/engine.render.architecture.md` → "Module Lifecycle: Static Init & Startup" for the full priority map and startup philosophy.

Font module slots:

- 290: `font.desc` (after VFS at 280, after job system at 102). At constructor time, initializes the descriptor pool AND dispatches startup jobs to pre-load default fonts. These jobs run on worker fibers, in parallel with everything else.
- 301: `font.atlas`, `font.sdf`, `font.msdf`. Initializes technique pools,
  registers Material Bases, subscribes to `mel_texture_pool_ready`. GPU resources
  are created when the texture pool fires its ready event (inside the boot cascade).

Font descriptor loading is the poster child for the boot system's parallel init:
file I/O and font parsing are pure CPU work dispatched as jobs from constructors.
By the time the boot cascade reaches the GPU phases, the parsed font data is
sitting in the descriptor pool waiting to be rasterized and uploaded.

### Boot System Integration

See `design/engine.boot.md` for the full event-driven boot architecture.

Font modules hook into the boot cascade:

- `font.desc` (priority 290): constructor dispatches CPU jobs to read and parse
  font files. These run on worker fibers, in parallel with everything else.
- `font.atlas/sdf/msdf` (priority 301): constructors subscribe to
  `mel_texture_pool_ready`. When the texture pool fires its ready event
  (after all render passes have compiled their shaders), font techniques
  initialize their GPU resources and fire their own ready events.

By the time the boot job signals completion and `app_init()` runs,
all default fonts are fully loaded, rasterized, and GPU-uploaded.
Zero "is it ready yet?" in user code.

---

## Foreplay

Preparatory refactoring of the existing font code to migrate toward the target architecture. These are ordered — each step builds on the previous one. None of these change external behavior (the example and demos should still work after each step).

### Step 1: Add `.format` to `Mel_Gpu_Texture_Opt` — DONE

File: `melody/gpu.texture.h`, `melody/gpu.texture.c`

Add format field to `Mel_Gpu_Texture_Opt`. Default to SRGB when 0 (zero-init). In `mel_gpu_texture_init_opt`, use `opt.format` instead of the hardcoded format.

Note: field currently uses `VkFormat` (not `Mel_Gpu_Format`) since the GPU type abstraction pass is still in progress. Will be updated when `gpu.types.h` lands.

### Step 2: Create `font.desc.h` / `font.desc.c` — DONE

New files: `melody/font.desc.h`, `melody/font.desc.fwd.h`, `melody/font.desc.c`

Define `Mel_Font_Desc` (technique-agnostic):

```c
struct Mel_Font_Desc {
    u8*             data;
    i64             data_size;
    stbtt_fontinfo  info;
    i32             ascent;
    i32             descent;
    i32             line_gap;
    i32             units_per_em;
};
```

Define `Mel_Font_Desc_Handle` (generational handle into the descriptor pool).

Implement `mel_font_desc_load_ttf(path, .on_finish = &counter)` — reserves a slot in the descriptor pool (returns handle immediately), dispatches a job that reads file via VFS, inits stb_truetype, extracts vertical metrics, and decrements the counter. Dedup by path hash (if already loaded, returns existing handle and skips the job).

The descriptor pool is a module-static `Mel_SlotMap` + `Mel_HashMap` inside `font.desc.c`, initialized by constructor at priority 290. The constructor also dispatches CPU jobs to pre-load default fonts — these run in parallel with the boot cascade and are ready before `app_init()`.

This step doesn't touch the existing technique code yet. The descriptor module exists alongside the old code.

### Step 3: Refactor techniques to take descriptors — DONE

Files: `melody/font.atlas.c`, `melody/font.sdf.c`, `melody/font.msdf.c` and their headers.

Each technique currently has a `_pool_load` function that does everything: read file → parse font → rasterize → upload. Split this:

- Extract the "read file + parse font" part — it's now `mel_font_desc_load_ttf` from Step 2
- The technique's `_create` function takes a `Mel_Font_Desc_Handle` instead of a file path
- Internally, it reads the descriptor's `stbtt_fontinfo` to access glyph data for rasterization

Example API change:
```c
// Before:
Mel_Font_Handle h = mel_font_sdf_pool_load(&pool, .path = S8("font.ttf"), .size = 40.0f);

// After:
Mel_Font_Desc_Handle desc = mel_font_desc_load_ttf(S8("font.ttf"));
Mel_Font_SDF_Handle h = mel_font_sdf_create(desc, .size = 40.0f, .px_range = 8.0f);
```

The old `Mel_Font_Descriptor` struct (which had UVs and technique-specific data) gets replaced by technique-entry-local glyph data. Each technique entry owns its own `Mel_Font_Glyph` array — same as today, but the shared descriptor is gone.

### Step 4: Typed handles — DONE

Files: `melody/font.atlas.fwd.h`, `melody/font.sdf.fwd.h`, `melody/font.msdf.fwd.h`

Currently everything uses `Mel_Font_Handle`. Create distinct types:

```c
typedef struct { Mel_SlotMap_Handle handle; } Mel_Font_Atlas_Handle;
typedef struct { Mel_SlotMap_Handle handle; } Mel_Font_SDF_Handle;
typedef struct { Mel_SlotMap_Handle handle; } Mel_Font_MSDF_Handle;
```

These are type-safe wrappers around the same slotmap handle. You can't accidentally pass an SDF handle to an atlas function. Update all function signatures and call sites.

### Step 5: Module-static pools — IN PROGRESS

Files: `melody/font.atlas.c`, `melody/font.sdf.c`, `melody/font.msdf.c`

Status: font.atlas.c and font.sdf.c have module-static pools with lazy init via `mel__font_*_ensure_init()`. GPU device binding via `mel__font_*_set_device()`. Remaining: constructor-based init, Material Base registration, `mel_texture_pool_ready` event listener.

Move pools from caller-owned to module-static:

```c
// font.sdf.c
static Mel_Font_SDF_Pool s_pool;
static bool s_gpu_initialized;

__attribute__((constructor(520)))
static void mel__font_sdf_init(void) {
    mel_slotmap_init(&s_pool.slotmap, mel_alloc_heap(),
        .item_size = sizeof(Mel_Font_SDF_Entry), .initial_capacity = 8);
    mel_hashmap_init(&s_pool.path_to_handle, ...);

    mel_material_base_register(&(Mel_Material_Base_Desc){
        .name = S8("text_sdf"),
        .param_size = sizeof(Mel_Text_SDF_Params),
        .shader = s_sdf_text_shader,
        .compat = MEL_COMPAT_FORWARD,
    });
}

__attribute__((destructor(520)))
static void mel__font_sdf_shutdown(void) {
    if (s_gpu_initialized)
        mel_font_sdf_pool_shutdown(&s_pool);
}
```

The public API no longer takes a pool pointer:

```c
// Before:
Mel_Font_SDF_Handle h = mel_font_sdf_pool_load(&pool, ...);

// After:
Mel_Font_SDF_Handle h = mel_font_sdf_create(desc, .size = 40.0f);
```

GPU initialization is triggered by the `mel_texture_pool_ready` event from the boot cascade. For default fonts loaded at startup, the technique pool's event listener initializes GPU resources and uploads textures as part of the cascade.

The pool is internal — the app never sees it unless it needs to extend or inspect font internals (in which case it uses `mel__font_sdf_pool()` double-underscore accessor per MEL-X-003).

### Step 6: Size normalization

Files: `melody/text.draw.c`, `melody/text.draw.h`

Replace `.scale` parameter with `.size`. The draw function computes `target_size / bake_size` internally.

Each technique entry stores `bake_size` (the size used during rasterization). The draw function reads it from the entry and divides.

```c
// Before:
mel_text_draw_font_sdf(&pool, handle, &list, text, .scale = 0.70f, .style = style);

// After (ECS component):
ecs_set(world, entity, Mel_Text_SDF, { .font = handle, .text = text, .size = 28.0f });
```

The pool pointer disappears from the draw API (it reads from the module-static pool internally). The caller thinks in target units, not scale factors.

### Step 7: Material Base registration

Files: `melody/font.atlas.c`, `melody/font.sdf.c`, `melody/font.msdf.c`

- Register Material Bases in constructors (priority 520):
  `text_atlas`, `text_sdf`, `text_msdf` — each with its own `param_size` and shader
- Material params (edge, softness, outline, px_range, colors) are stored in the
  Manager's material storage buffer, indexed by `Mel_Render_Info.material_idx`
- The Pipeline reads material params from the storage buffer during `draw`
- No families, no templates, no backends — just flat Material Base registrations

### Step 8: Async creation

Files: `melody/font.desc.c`, `melody/font.atlas.c`, `melody/font.sdf.c`, `melody/font.msdf.c`

Add `Mel_Counter* on_finish` to descriptor load opt and technique create opt. The functions dispatch work via `mel_job_run`:

- Descriptor load: job reads file via VFS + parses with stb_truetype + stores in pool + decrements counter
- Technique create: job reads descriptor + rasterizes + uploads texture + populates glyph data + decrements counter

Handle is returned immediately (slot reserved). Data becomes valid after counter fires. Draw calls before ready are no-ops.

### Step 9: Update examples and demos

Update `examples/example.text.techniques.c` and any demos using fonts to use the new API:

```c
Mel_Font_Desc_Handle desc = mel_font_desc_load_ttf(S8("assets/fonts/monaco.ttf"));
Mel_Font_Atlas_Handle atlas = mel_font_atlas_create(desc, .size = 28.0f);
Mel_Font_SDF_Handle sdf = mel_font_sdf_create(desc, .size = 40.0f, .px_range = 8.0f);
Mel_Font_MSDF_Handle msdf = mel_font_msdf_create(desc, .size = 128.0f, .px_range = 12.0f);

ecs_entity_t e1 = ecs_new(world);
ecs_set(world, e1, Mel_Transform_2D, { .pos = { 10, 10 } });
ecs_set(world, e1, Mel_Text_Atlas, { .font = atlas, .text = S8("Bitmap Atlas"), .size = 28.0f });

ecs_entity_t e2 = ecs_new(world);
ecs_set(world, e2, Mel_Transform_2D, { .pos = { 10, 50 } });
ecs_set(world, e2, Mel_Text_SDF, { .font = sdf, .text = S8("Signed Distance Field"), .size = 28.0f });

ecs_entity_t e3 = ecs_new(world);
ecs_set(world, e3, Mel_Transform_2D, { .pos = { 10, 90 } });
ecs_set(world, e3, Mel_Text_MSDF, { .font = msdf, .text = S8("Multi-Channel SDF"), .size = 28.0f });
```

No pools, no scale factors, no manual init, no per-frame draw calls.
Text entities go through the Source → Manager → Pipeline path
automatically. The example gets radically simpler.

---

## Three-Level Architecture: Loader -> Descriptor -> Technique Handle

### Level 0: Font Loaders

Different font file formats have different loaders. Each loader produces the same output: a `Mel_Font_Desc`.

```c
Mel_Font_Desc_Handle desc = mel_font_desc_load_ttf(S8("assets/fonts/monaco.ttf"));
```

The loader reads the file, parses it, extracts the shared metrics, and stores the result in the descriptor pool. If the same path was already loaded, it returns the existing handle (dedup by path+format).

Adding a new font format means adding a new loader function. The descriptor struct, the technique handles, the draw code — none of that changes.

Possible future loaders:
- `mel_font_desc_load_otf(path)` — OpenType
- `mel_font_desc_load_bmfont(path)` — BMFont bitmap font descriptions
- `mel_font_desc_load_mem(data, size)` — from memory buffer

### Level 1: Font Descriptor

A font descriptor represents a parsed font. It is technique-agnostic — no atlas, no textures, no UVs. It holds the data needed to rasterize glyphs in any technique.

```c
struct Mel_Font_Desc {
    u8*             data;           // raw font file bytes (owned)
    i64             data_size;
    stbtt_fontinfo  info;           // parsed font info (stb_truetype for TTF)
    i32             ascent;         // font units
    i32             descent;        // font units (negative)
    i32             line_gap;       // font units
    i32             units_per_em;
};
```

The descriptor pool lives as a static inside `font.desc.c`. No core.engine involvement.

**Lifetime**: A descriptor can be released after all technique handles are created from it. Technique entries own their own data (glyph arrays, textures). There is no reference back to the descriptor at draw time. The descriptor is only needed during technique handle creation.

### Level 2: Technique Handles

A technique handle is a rasterized font ready to draw. Created from a descriptor.

```c
Mel_Font_Atlas_Handle atlas = mel_font_atlas_create(desc, .size = 28.0f);
Mel_Font_SDF_Handle   sdf   = mel_font_sdf_create(desc, .size = 40.0f, .px_range = 8.0f);
Mel_Font_MSDF_Handle  msdf  = mel_font_msdf_create(desc, .size = 128.0f, .px_range = 12.0f);
```

Each technique has different creation parameters because the techniques work differently:

- **Atlas**: `.size` is the rasterization size. This IS the display size. Scaling an atlas font looks bad (that's the point).
- **SDF**: `.size` is the bake size. Can be displayed at any size. `.px_range` controls the distance field range.
- **MSDF**: `.size` is the bake size (typically large). Can be displayed at any size. `.px_range` controls the distance field range.

Each technique pool is a static inside its own module (`font.atlas.c`, `font.sdf.c`, `font.msdf.c`). No core.engine involvement.

---

## Async Loading

All loading is async. Both descriptor loading and technique handle creation dispatch jobs to worker fibers. The engine exploits this for free multi-threaded startup.

### Startup Path (engine-managed, automatic)

Default fonts are loaded at startup without any user code. The `font.desc`
constructor (priority 290) dispatches CPU jobs that run on worker fibers:

```c
__attribute__((constructor(290)))
static void mel__font_desc_init(void) {
    mel_slotmap_init(&s_desc_pool, ...);
    mel_job_run(nullptr, mel__load_default_font, nullptr);
}

static void mel__load_default_font(void* data) {
    u8* ttf_data = mel_vfs_read_file(S8("assets/fonts/default.ttf"), &size, alloc);
    stbtt_InitFont(&info, ttf_data, 0);
}
```

This job runs in parallel with the boot cascade. By the time the boot
job fires `mel_gpu_device_ready` and the cascade reaches `mel_texture_pool_ready`,
the font file is already parsed and sitting in the descriptor pool. The
font technique listener initializes GPU resources and uploads textures as
part of the cascade. `app_init()` runs with fonts fully loaded.

If the app loads 10 fonts at startup, all 10 file reads and parses happen in parallel. Startup time = the slowest single font, not the sum.

### Runtime Path (user-managed, explicit)

For fonts loaded after startup (user-triggered, level change, etc.), the caller provides their own counter:

```c
Mel_Counter done = MEL_COUNTER_INIT;
Mel_Font_Desc_Handle desc = mel_font_desc_load_ttf(S8("assets/fonts/monaco.ttf"), .on_finish = &done);
// handle is valid immediately (slot reserved), but data isn't populated yet
// ... do other work ...
mel_counter_wait(&done);
// now the descriptor is ready to use
```

The loader dispatches a job that:
1. Reads the font file via VFS (which can itself use AIO)
2. Parses the font data (stb_truetype init, extract metrics)
3. Stores the result in the descriptor pool
4. Decrements the counter

The handle is returned immediately (the slot is reserved in the pool). The data behind it becomes valid after the counter fires.

### Technique handle creation

```c
Mel_Counter done = MEL_COUNTER_INIT;
Mel_Font_SDF_Handle sdf = mel_font_sdf_create(desc, .size = 40.0f, .on_finish = &done);
// ... do other work ...
mel_counter_wait(&done);
// now the SDF font is ready to draw
```

The create function dispatches a job that:
1. Reads glyph data from the descriptor
2. Rasterizes the atlas/SDF/MSDF (CPU-heavy, benefits from worker fibers)
3. Uploads the texture to GPU (staging buffer + immediate submit)
4. Populates glyph metrics
5. Decrements the counter

For MSDF especially, rasterization is expensive (~3s for a full charset at 2048x2048). Running this on a worker fiber keeps the main thread responsive.

### Chaining

Since both steps use counters, they can be chained:

```c
Mel_Counter desc_done = MEL_COUNTER_INIT;
Mel_Font_Desc_Handle desc = mel_font_desc_load_ttf(path, .on_finish = &desc_done);

mel_counter_wait(&desc_done);

Mel_Counter sdf_done = MEL_COUNTER_INIT;
Mel_Font_SDF_Handle sdf = mel_font_sdf_create(desc, .size = 40.0f, .on_finish = &sdf_done);
```

Or the whole sequence can be wrapped in a single job that loads a descriptor and creates multiple technique handles from it. The startup path does exactly this — one job per font that handles desc load → technique creation → GPU upload as a single chain.

### Drawing before ready

If a technique handle is used for drawing before its counter fires, the draw call is a no-op (the entry has no glyph data yet). No crash, no assert — just no text until the font is ready. This allows fire-and-forget loading where text appears as soon as the font finishes loading.

For startup fonts this never happens — the boot cascade guarantees they're ready before `app_init()`. For runtime-loaded fonts, the no-op behavior provides a graceful loading experience (text pops in when the font is ready).

---

## Size Normalization

The caller should never compute scale factors. Draw calls take a target `.size`, and the engine computes the ratio internally.

```c
ecs_set(world, label, Mel_Text_SDF, { .font = sdf, .text = S8("Hello"), .size = 28.0f });
```

The SDF was baked at 40px. The Source's sync computes `28 / 40 = 0.70` and applies it when writing glyph data. The caller just says "28 units tall."

The `.size` value is unitless from the font system's perspective. It means "this many units in whatever coordinate space the View's camera operates in." If the View has a screen-space HUD camera, `.size = 28` means 28 pixels. If it has a world-space camera, `.size = 28` means 28 world units. The font system does not care — it produces glyph data at the requested size, and the projection matrix determines what that means on screen.

For atlas fonts, `.size` still works but the result looks best when it matches the bake size. Drawing an atlas font at a different size produces pixel artifacts — which is intentional and demonstrable.

Each technique entry stores its bake size. The draw function divides `target_size / bake_size` to get the internal scale applied to glyph metrics.

---

## Text Rendering Pipeline

Text rendering is a pipeline of discrete stages. The current implementation collapses all stages into a single draw function. The architecture defines each stage and the data flowing between them so that future features (rich text, shaping, effects) plug into specific stages without breaking the rest.

### Pipeline Stages

```
[1. Parse] → [2. Shape] → [3. Resolve] → [4. Layout] → [5. Effect] → [6. Render]
```

1. **Parse**: Rich text markup → text runs. Each run has a font handle, material, and substring range. *Today*: skipped — entire string is one run.

2. **Shape**: Codepoints → glyph IDs with positioning offsets. A text shaper (e.g. HarfBuzz) handles ligatures, bidirectional reordering, and context-dependent glyph substitution. *Today*: simple 1:1 codepoint-to-glyph mapping, left-to-right only.

3. **Resolve**: For each glyph, check if the technique handle has it. If not, try the next font in the fallback chain. *Today*: skipped — single font, missing glyphs produce tofu.

4. **Layout**: Runs of shaped glyphs → positioned lines. Handles line breaking, word wrapping, alignment (left/center/right/justify), and vertical spacing. *Today*: simple left-to-right, `\n` for line breaks.

5. **Effect**: Per-glyph transform and color modification. Wave, shake, typewriter reveal, fade-in, rainbow. Applied as post-layout modifiers before rendering. *Today*: skipped — no per-glyph modification.

6. **Render**: Positioned glyphs → glyph buffer entries in the Manager. The Source's `sync` writes glyph data, the Pipeline's `draw` generates quads on the GPU.

### Intermediate Representation: Positioned Glyph Buffer

The data flowing between Layout (stage 4) and Effect/Render (stages 5-6) is the key intermediate type:

```c
typedef struct {
    u32 codepoint;
    u32 glyph_index;
    f32 x;
    f32 y;
    f32 scale;
    f32 rotation;               // radians, 0 = upright (curved text sets this)
    Mel_Vec4 color_mod;         // multiplicative RGBA modifier (default: 1,1,1,1)
    u32 run_index;
    u32 flags;                  // MEL_TEXT_GLYPH_VISIBLE, MEL_TEXT_GLYPH_HIDDEN (typewriter), etc.
} Mel_Text_Positioned_Glyph;

typedef struct {
    Mel_Text_Positioned_Glyph* glyphs;
    u32 glyph_count;
    f32 width;
    f32 height;
    u32 line_count;
} Mel_Text_Layout_Result;
```

Each glyph knows its position in text-local space, its rotation (for curved text), a per-glyph color modifier (for rainbow, fade, etc.), which run it belongs to (and therefore which font + material), and its scale from size normalization. The `flags` field controls visibility (typewriter effect hides glyphs beyond an index) and other per-glyph state.

**Why this matters**: every future feature operates on a specific stage boundary.

- Rich text replaces stage 1 (parse) — produces multiple runs from markup
- Text shaping replaces stage 2 — HarfBuzz consumes codepoints, produces glyph IDs + offsets
- Font fallback operates between stages 2-3 — "glyph not found" triggers retry with next font
- Text layout is stage 4 — wrapping and alignment algorithms operate on shaped glyph runs
- Per-character effects operate at stage 5 — modify positions/colors of already-laid-out glyphs
- Dynamic atlas operates at stage 6 — "glyph not rasterized yet" triggers on-demand bake

### Shortcut Path

The ECS component (`Mel_Text_SDF`) collapses stages 1-6 internally.
The Source's `sync` produces a single run, does 1:1 glyph mapping,
simple left-to-right layout, no effects, and writes glyph data into
the Manager. This is the fast path for common cases.

The pipeline stages are exposed when you need explicit control:

```c
Mel_Text_Layout_Result layout = mel_text_layout(sdf, S8("Hello World"),
    .size = 28.0f, .max_width = 200.0f, .align = MEL_TEXT_ALIGN_CENTER);

mel_text_source_add_layout(source, sdf, &layout,
    .x = 10, .y = 10, .style = style);

mel_text_layout_free(&layout);
```

Or with effects:

```c
Mel_Text_Layout_Result layout = mel_text_layout(sdf, text, .size = 28.0f);
mel_text_effect_wave(&layout, .amplitude = 3.0f, .frequency = 2.0f, .time = t);
mel_text_source_add_layout(source, sdf, &layout, .x = 10, .y = 10, .style = style);
mel_text_layout_free(&layout);
```

The layout result is a plain data buffer. Effects mutate it in place.
`_add_layout` writes the final glyph data into the Manager. Each stage
is independently replaceable.

---

## Drawing

Text objects live in the Render Manager like everything else. The Source's
`sync` function creates Manager handles, writes transforms and material
params, and populates a glyph buffer. The Pipeline's `draw` reads the
glyph buffer and generates quads on the GPU.

### One handle per text label

A text label "Hello World" is one Manager object, not one object per glyph:

- **Transform**: position of the text block origin
- **Bounds**: bounding box of the entire text block (for culling)
- **Info**: `material_base_id` = text technique, `material_idx` = appearance
  params, `mesh_idx` = glyph buffer range (start + count)

The per-glyph data (character UVs, positions relative to label origin)
lives in a glyph storage buffer. The Manager stores where each label's
glyphs are in that buffer.

### ECS path (retained, automatic)

```c
ecs_entity_t label = ecs_new(world);
ecs_set(world, label, Mel_Transform_2D, { .pos = { 100, 50 } });
ecs_set(world, label, Mel_Text_SDF, {
    .font = sdf,
    .text = S8("Hello World"),
    .size = 28.0f,
    .style = mel_text_style(mel_vec4(1, 1, 1, 1)),
});
```

The ECS Source's `sync` function detects this entity (via flecs observers
or change detection), runs text pipeline stages 1-5 (layout the glyphs),
allocates a Manager handle, writes transform/bounds/info/material params,
and writes glyph data into the glyph buffer.

On subsequent frames, if nothing changed on this entity, delta-sync
skips it entirely. If the text changes, sync re-layouts and re-uploads
only this label's glyph range.

### Manual Source path (programmatic)

For non-ECS users, a manual Source provides the same functionality:

```c
Mel_Text_Object text_obj = mel_text_source_add(text_source, sdf,
    S8("Score: 1000"),
    .x = 10, .y = 10, .size = 28.0f,
    .style = mel_text_style(mel_vec4(1, 1, 1, 1)),
);

mel_text_source_update(text_source, text_obj, S8("Score: 1500"));
mel_text_source_remove(text_source, text_obj);
```

Under the hood, `_add` allocates a Manager handle, `_update` marks
it dirty (delta-sync re-uploads), `_remove` frees the handle.

### Transient text (debug overlay, frame counter)

For text that genuinely changes every frame, a transient Source
auto-frees all handles at the end of each frame:

```c
mel_text_transient_push(transient_source, sdf,
    S8("FPS: 60"), .x = 10, .y = 10, .size = 14.0f);
```

This allocates a handle, writes glyph data, and the Source's sync
clears everything at the start of the next frame. Simple, but
re-uploads every frame (acceptable for debug text, not ideal for
production UI).

### Why not render lists?

The old architecture pushed `Mel_Text_Entry` quads into render lists
every frame. This re-uploads ALL text every frame even if nothing
changed. The Manager approach uploads only what changed (delta-sync).
For a UI with 100 static labels, that's the difference between
uploading 100 labels per frame vs uploading zero.

Render lists are gone. Text goes through the Manager via Sources.

---

## Measurement

```c
Mel_Vec2 size = mel_text_measure_atlas(atlas, S8("Hello"), .size = 28.0f);
Mel_Vec2 size = mel_text_measure_sdf(sdf, S8("Hello"), .size = 28.0f);
Mel_Vec2 size = mel_text_measure_msdf(msdf, S8("Hello"), .size = 28.0f);
```

Same normalization applies. Returns the bounding box in the same unit space as `.size`.

---

## Internal Data

Each technique entry holds:

```c
struct Mel_Font_Atlas_Entry {
    Mel_Font_Glyph* glyphs;        // per-glyph UVs + offsets (at bake scale)
    u32             glyph_count;
    u32             first_codepoint;
    f32             ascent;         // at bake scale
    f32             line_height;    // at bake scale
    f32             bake_size;      // for normalization
    Mel_Gpu_Texture texture;
};
```

SDF and MSDF entries are similar but add `px_range` and use UNORM textures instead of SRGB.

The existing `Mel_Font_Descriptor` struct gets absorbed into each technique entry (it was never truly shared — it contains UVs which are technique-specific).

---

## Module Lifecycle

See `design/engine.boot.md` for the full boot system architecture.

Each font module uses constructors for data structure init + event
subscriptions. GPU work is triggered by boot cascade events.

- `font.desc.c` (priority 290) — after VFS. Inits descriptor pool,
  dispatches CPU jobs to parse default fonts.
- `font.atlas.c` (priority 301) — subscribes to `mel_texture_pool_ready`.
  GPU init triggered by event listener.
- `font.sdf.c` (priority 301) — same pattern.
- `font.msdf.c` (priority 301) — same pattern.

Shutdown is event-driven: each module subscribes to `mel_shutdown_begin`
and cleans up GPU resources in its listener. Destructors handle data
structure cleanup.

---

## Adding A New Font Technique

1. Create `font.X.h` / `font.X.c`
2. Define `Mel_Font_X_Handle`, entry struct with glyphs + GPU data
3. Implement `mel_font_X_create(desc, ...)` with `_opt` pattern
4. Register a Material Base (`text_X`) in the constructor
5. Pool is a static in `font.X.c`, initialized by constructor
6. Add `Mel_Text_X` ECS component — the ECS Source's `sync` handles it

Nothing else changes. No central registry, no type dispatch.

---

## Material System Integration

Text appearance integrates with the rendering architecture's Material Base
system (see `design/engine.render.architecture.md`).

### Material Bases for Text

Each font technique registers a Material Base at startup:

```c
mel_material_base_register(&(Mel_Material_Base_Desc){
    .name = S8("text_sdf"),
    .param_size = sizeof(Mel_Text_SDF_Params),
    .shader = s_sdf_text_shader,
    .compat = MEL_COMPAT_FORWARD,
});
```

Atlas, SDF, and MSDF each register their own Material Base. The
`param_size` determines how much space each object's material params
occupy in the Manager's material storage buffer.

### Material Params

Text material params are a flat struct stored in the Manager's
material storage buffer, indexed by `Mel_Render_Info.material_idx`:

```c
typedef struct Mel_Text_SDF_Params {
    Mel_Vec4 color;
    Mel_Vec4 outline_color;
    f32 edge;
    f32 softness;
    f32 outline;
    f32 px_range;
    f32 shadow_offset_x;
    f32 shadow_offset_y;
    f32 shadow_softness;
    f32 glow_radius;
} Mel_Text_SDF_Params;
```

The Pipeline's `draw` function reads these from the storage buffer.
The shader accesses them via `materials[info.material_idx]`.

### Creating Text Objects with Materials

The Source's `sync` function writes material params into the Manager:

```c
Mel_Render_Handle h = mel_mgr_alloc(mgr);
mel_mgr_set_transform(mgr, h, transform);
mel_mgr_set_bounds(mgr, h, bounds);
mel_mgr_set_info(mgr, h, (Mel_Render_Info){
    .material_base_id = s_text_sdf_base_id,
    .material_idx = mat_slot,
    .mesh_idx = glyph_range_idx,
    .layer_mask = MEL_LAYER_DEFAULT,
});
```

The material params at `mat_slot` contain the SDF appearance
(color, edge, softness, outline, etc.). Changing appearance is
a `mel_mgr_set_info` call — delta-sync uploads only what changed.

### Convenience: Mel_Text_Style

For simple cases, `Mel_Text_Style` remains as a lightweight struct
that the Source translates into material params internally:

```c
Mel_Text_Style style = mel_text_style(mel_vec4(1, 1, 1, 1));
```

Under the hood, the Source packs this into `Mel_Text_SDF_Params`
with default edge/softness/outline values and writes it to the
Manager's material storage. No manual material setup required.

### What This Buys

- Text appearance lives in the same GPU storage buffers as mesh/sprite materials
- Delta-sync: static text with unchanged appearance costs zero upload
- The Pipeline reads text materials the same way it reads mesh materials
- Material params are inspector-editable (same storage, same access pattern)
- Simple path (`mel_text_style(white)`) pays no extra complexity — the Source handles the translation

---

## Resolved Questions

**Unicode**: Support codepoint ranges at creation time. The caller specifies which ranges to include when creating a technique handle (e.g. `.ranges = { MEL_UNICODE_LATIN, MEL_UNICODE_CYRILLIC }`). Glyphs outside the baked ranges produce a fallback (tofu/missing glyph). Glyph-on-demand (dynamic atlas expansion) is a future optimization, not a launch requirement.

**Kerning**: stb_truetype provides `stbtt_GetCodepointKernAdvance` and `stbtt_GetGlyphKernAdvance`. The draw/measure functions query kern pairs during text layout and add the kern offset to xadvance. This is per-technique-entry data since kern values scale with bake size. Cost is one extra lookup per glyph pair during layout — negligible.

**Text layout (wrapping, alignment, line breaking)**: Separate system built on top of measurement. A `mel_text_layout` function takes a font handle, text, max width, and alignment, and produces a list of positioned lines/runs. The draw functions can then take a layout result instead of raw text. This is a later layer — the font system provides the primitives (glyph metrics, kerning, measurement), the layout system composes them.

**MSDF library quality**: The vendored msdf-c (exezin/msdf-c) has known issues: "glyph alignment seems off", "error correction appears to be wrong (pixel clash)". Error correction is already disabled in our vendored copy. For now, it works well enough after fixing the scale mismatch. Long-term options: fix the library, write our own MSDF generator, or integrate msdfgen (C++, heavier but battle-tested). Not blocking for vNext.

**Texture format**: Add a `.format` field to `Mel_Gpu_Texture_Opt` (defaulting to `MEL_FORMAT_RGBA8_SRGB` for backwards compat). SDF/MSDF pass `MEL_FORMAT_RGBA8_UNORM`. This eliminates the manual staging buffer + image init + sampler creation dance in the font modules.

---

## Font Fallback Chains

A font stack wraps multiple technique handles of the same type. When a glyph is missing from the primary font, the next font in the chain is tried.

```c
Mel_Font_SDF_Handle latin = mel_font_sdf_create(latin_desc, .size = 40.0f);
Mel_Font_SDF_Handle cjk   = mel_font_sdf_create(cjk_desc,   .size = 40.0f);
Mel_Font_SDF_Handle emoji = mel_font_sdf_create(emoji_desc, .size = 40.0f);

Mel_Font_Stack_Handle stack = mel_font_stack_create(
    .fonts = (Mel_Font_SDF_Handle[]){ latin, cjk, emoji },
    .count = 3,
);
```

The stack handle is usable anywhere a single technique handle is accepted. Draw and measure functions iterate the chain internally — the caller doesn't know or care that multiple fonts are involved.

**Architectural constraint**: all fonts in a stack must be the same technique (all SDF, all MSDF, etc.). Mixing techniques in a single stack would require per-glyph mode switching in the glyph buffer (each glyph entry carries a `mode` field). If cross-technique stacks become necessary, the mode and texture would vary per glyph — technically possible but adds complexity to the resolve stage.

**Glyph presence query**: technique entries need a `mel_font_X_has_glyph(handle, codepoint)` function. This is a simple bounds check against the baked codepoint range, or a lookup into the glyph array. Fast enough to call per-codepoint during resolve.

**Interaction with dynamic atlas**: if a glyph is missing from font A but font A has dynamic atlas enabled, do we rasterize on demand or fall through to font B? Answer: fall through first. Dynamic atlas handles glyphs that are *in the font file* but not yet rasterized. Fallback handles glyphs that are *not in the font file at all*. These are different problems.

---

## SDF Effects

SDF and MSDF techniques enable visual effects that are essentially free — they're just additional threshold comparisons in the fragment shader. These effects are controlled through material parameters.

### Drop Shadow

Offset the distance field sample and blend a shadow color behind the main text. Material params:
- `shadow_offset_x`, `shadow_offset_y` — texel offset (in `params1.xy`)
- `shadow_softness` — wider smoothstep range for softer shadow (in `params1.z`)
- Shadow color is `emissive_color` (outline_color) when shadow is enabled without outline, or a dedicated shadow color param if both are needed simultaneously

### Glow

Extend the SDF threshold outward to create an aura. Material params:
- `glow_radius` — how far beyond the glyph edge the glow extends (in `params1.w`)
- Glow color is derived from `base_color` with reduced alpha, or from `emissive_color` depending on whether outline is also active

### Inner Shadow / Bevel

Sample the distance field with a slight offset to create a directional inner shadow, simulating depth. This uses the same shadow offset params but applies them inverted and only inside the glyph boundary.

### Effect Stacking

Multiple effects can be active simultaneously. The shader evaluates them in order: glow → shadow → outline → fill. Each layer blends on top of the previous one. The material params control which layers are active (zero values disable a layer — e.g. `glow_radius = 0` means no glow).

### Atlas Technique

Atlas fonts do not support SDF effects (no distance field data). The material params for edge, softness, outline, shadow, and glow are ignored for atlas mode. Atlas materials only use `base_color`. This is by design — atlas is the "simple and fast" path.

---

## Rich Text

Rich text allows mixed fonts, sizes, colors, and materials within a single text block. This operates at pipeline stage 1 (Parse).

### Markup

```c
ecs_set(world, label, Mel_Text_Rich_SDF, {
    .font = sdf,
    .text = S8("[color=#ff0000]Critical:[/color] HP is low!"),
    .size = 28.0f,
    .style = mel_text_style(mel_vec4(1, 1, 1, 1)),
});
```

The parser produces runs from markup tags. Each run inherits from the base material unless overridden by a tag. Supported tags (extensible):

- `[color=#rrggbb]` / `[color=red]` — override text color
- `[outline=#rrggbb]` — override outline color
- `[size=N]` — override size for this run
- `[font=handle_name]` — switch font for this run (requires a named font registry or handle lookup)
- `[b]`, `[i]` — bold/italic (requires corresponding technique handles loaded)
- `[wave]`, `[shake]`, `[typewriter]` — per-character effect triggers (pipeline stage 5)

### Text Runs

```c
typedef struct {
    u32 start;
    u32 end;
    Mel_Font_Handle font;
    u32 material_idx;
    f32 size;
    u32 effect_flags;
} Mel_Text_Run;
```

The parse stage produces an array of runs. Layout and rendering iterate over runs. This is the seam where rich text plugs in without affecting anything downstream.

### Programmatic API

Markup isn't the only way to create runs. For full control:

```c
Mel_Text_Run runs[] = {
    { .start = 0, .end = 9,  .font = bold_sdf, .material_idx = red_idx, .size = 32.0f },
    { .start = 9, .end = 20, .font = sdf,      .material_idx = white_idx, .size = 28.0f },
};
mel_text_source_add_runs(source, runs, 2, S8("Critical: HP is low!"),
    .x = 10, .y = 10);
```

---

## Per-Character Effects

Effects operate at pipeline stage 5. They take a `Mel_Text_Layout_Result` and modify glyph positions, colors, or visibility in place.

### Built-in Effects

- **Wave**: sinusoidal y-offset per glyph. `mel_text_effect_wave(&layout, .amplitude, .frequency, .time, .phase_per_char)`
- **Shake**: random position jitter per glyph. `mel_text_effect_shake(&layout, .intensity, .seed)`
- **Typewriter**: glyphs beyond a character index are hidden. `mel_text_effect_typewriter(&layout, .visible_count)`. The caller increments `visible_count` over time.
- **Fade-in**: per-glyph alpha ramp from a start index. `mel_text_effect_fade(&layout, .start_index, .fade_length)`
- **Rainbow**: per-glyph hue rotation. `mel_text_effect_rainbow(&layout, .offset, .spread)`

### Custom Effects

Effects are just functions that take `Mel_Text_Layout_Result*`. Users write their own:

```c
void my_custom_effect(Mel_Text_Layout_Result* layout, f32 time) {
    for (u32 i = 0; i < layout->glyph_count; i++) {
        layout->glyphs[i].y += sinf(time + i * 0.3f) * 5.0f;
    }
}
```

### Effect Integration with Rich Text

Rich text tags like `[wave]` set `effect_flags` on the run. The render path checks these flags and applies the corresponding effect to glyphs in that run's range. This connects stage 1 (parse) to stage 5 (effect) through the run metadata.

---

## Dynamic Atlas

Technique handles can optionally support glyph-on-demand rasterization. This is critical for CJK support (tens of thousands of possible glyphs — pre-baking is impractical).

### Architecture

A dynamic technique handle starts with a base set of rasterized glyphs (e.g. ASCII). When the render stage encounters a codepoint that isn't in the atlas:

1. Rasterize the glyph (same technique: bitmap, SDF, or MSDF)
2. Pack it into the atlas texture (shelf/skyline packing into free space)
3. If the atlas is full, resize (create larger texture, copy existing data, remap UVs)
4. Update the glyph entry with new UVs
5. Mark the atlas texture as dirty (descriptor update needed)

### Creation

```c
Mel_Font_SDF_Handle sdf = mel_font_sdf_create(desc,
    .size = 40.0f,
    .dynamic = true,
    .initial_ranges = { MEL_UNICODE_BASIC_LATIN },
);
```

When `.dynamic = true`, the technique entry keeps a reference back to the font descriptor (the descriptor must NOT be released while dynamic handles exist). Rasterization of new glyphs requires the raw font data.

### Thread Safety

Dynamic rasterization can happen from any thread during layout/rendering. The atlas packing and texture upload must be synchronized. Options:

- Rasterize on worker fiber, push to a pending queue, upload batch on main thread before rendering
- Use a lock-free glyph request queue — the render stage adds requests, a background job processes them, glyphs appear next frame

Glyphs requested but not yet rasterized produce tofu for one frame, then appear. This is acceptable and matches how web browsers handle dynamic font loading.

### Interaction with Descriptor Lifetime

The "descriptor can be released after technique handle creation" rule has an exception for dynamic handles. A dynamic technique handle needs the descriptor alive to rasterize new glyphs. The technique entry's documentation should make this clear.

---

## Text Shaping

Text shaping converts a Unicode string into a sequence of positioned glyph IDs, handling ligatures (fi → single glyph), contextual forms (Arabic initial/medial/final), bidirectional reordering (mixed Latin + Arabic), and complex script layout (Devanagari conjuncts, Thai tone marks).

### Pipeline Integration

Shaping replaces pipeline stage 2 (Shape). Without shaping, we do simple 1:1 codepoint-to-glyph mapping. With shaping, HarfBuzz (or equivalent) consumes codepoints and produces glyph IDs + x/y offsets.

### Shaper Interface

```c
typedef struct {
    u32 glyph_id;
    f32 x_offset;
    f32 y_offset;
    f32 x_advance;
    f32 y_advance;
    u32 cluster;            // maps back to source codepoint index
} Mel_Shaped_Glyph;

typedef struct {
    Mel_Shaped_Glyph* glyphs;
    u32 glyph_count;
} Mel_Shape_Result;
```

The shaper is a pluggable function pointer on the text pipeline context:

```c
typedef Mel_Shape_Result (*Mel_Text_Shaper_Fn)(
    Mel_Font_Desc_Handle desc,
    str8 text,
    u32 script,             // ISO 15924 script code
    u32 direction,          // LTR, RTL, TTB
    void* user
);
```

The default shaper is the simple 1:1 mapper. If HarfBuzz is linked, a HarfBuzz shaper replaces it. The font descriptor exposes the raw font data that HarfBuzz needs (`hb_blob_create` from `Mel_Font_Desc.data`).

### Impact on Descriptor

The font descriptor must keep its raw font data alive if shaping is used, because HarfBuzz creates an `hb_font_t` from the raw bytes. This is already the case — the descriptor owns `data`. The shaper creates an `hb_font_t` lazily on first shape call and caches it in the descriptor (or in a side table keyed by descriptor handle).

### Bidirectional Text

Bidi reordering (UAX #9) is part of shaping. The shaper must know the paragraph direction and resolve per-run directions. This happens before glyph substitution. The output `Mel_Shaped_Glyph` array is in visual order (left-to-right for display), regardless of logical order.

Rich text runs (stage 1) may explicitly set direction: `[rtl]` or `[ltr]`. Without explicit direction, the shaper uses the Unicode Bidirectional Algorithm to detect it.

---

## Vertical Text Layout

CJK text can flow top-to-bottom in right-to-left columns. Japanese also has tate-chu-yoko (horizontal numbers embedded in vertical text).

### Pipeline Integration

Vertical layout operates at pipeline stage 4 (Layout). The layout function takes a direction parameter:

```c
Mel_Text_Layout_Result layout = mel_text_layout(sdf, text,
    .size = 28.0f,
    .direction = MEL_TEXT_DIR_TTB,       // top-to-bottom
    .max_height = 400.0f,                // column height (analogous to max_width for horizontal)
    .column_gap = 10.0f,
);
```

### Vertical Metrics

Fonts may include vertical metrics (vmtx/vhea tables in OpenType). When present, the descriptor exposes them:

```c
struct Mel_Font_Desc {
    // ... existing fields ...
    bool    has_vertical_metrics;
    i32     vert_ascent;        // font units
    i32     vert_descent;       // font units
};
```

If vertical metrics are absent, the layout system synthesizes them from horizontal metrics (common fallback: use em-square width, center glyphs). stb_truetype doesn't directly expose vmtx — this requires additional table parsing in the TTF loader, or an OpenType-aware loader.

### Glyph Rotation

In vertical CJK layout, most CJK characters are upright, but Latin characters are rotated 90° clockwise. The `rotation` field on `Mel_Text_Positioned_Glyph` handles this. The layout stage sets rotation per glyph based on Unicode vertical orientation properties (UTR #50).

---

## Curved / Path Text

Text placed along a parametric curve (bezier, arc, circle, arbitrary path).

### Pipeline Integration

Curved text is a post-layout transform — it operates at pipeline stage 5 (Effect), same as wave/shake. It takes a `Mel_Text_Layout_Result` where glyphs are laid out linearly, and re-positions each glyph along a curve.

```c
Mel_Text_Layout_Result layout = mel_text_layout(sdf, S8("Along the curve"), .size = 28.0f);

Mel_Vec2 control_points[] = {
    mel_vec2(50, 200), mel_vec2(200, 50), mel_vec2(350, 200),
};
mel_text_effect_path(&layout,
    .points = control_points,
    .point_count = 3,
    .path_type = MEL_TEXT_PATH_QUADRATIC_BEZIER,
);

mel_text_source_add_layout(source, sdf, &layout, .x = 0, .y = 0, .style = style);
```

### Per-Glyph Positioning

For each glyph, the effect computes:
1. Arc-length position along the curve (from the glyph's linear x position)
2. Position on the curve at that arc length
3. Tangent angle at that point → sets `glyph.rotation`
4. Optional offset perpendicular to the tangent (for baseline offset)

The `rotation` field on `Mel_Text_Positioned_Glyph` is what makes this work. The render stage (stage 6) must respect per-glyph rotation when generating quads — each quad gets a per-glyph rotation applied to its vertex positions.

### Path Types

- `MEL_TEXT_PATH_QUADRATIC_BEZIER` — quadratic bezier from control points
- `MEL_TEXT_PATH_CUBIC_BEZIER` — cubic bezier
- `MEL_TEXT_PATH_ARC` — circular arc (center, radius, start angle, end angle)
- `MEL_TEXT_PATH_CUSTOM` — user-provided function: `f32 t → (Mel_Vec2 pos, f32 angle)`

---

## Subpixel Rendering

LCD subpixel rendering (ClearType-style) rasterizes glyphs at 3x horizontal resolution, exploiting the physical RGB subpixel layout of LCD displays to triple effective horizontal resolution.

### Architecture Level

This is a new technique at Level 2: `font.subpixel.c`. The descriptor is unchanged. The pipeline is unchanged. The difference is entirely in rasterization and shader.

### Rasterization

The atlas stores each glyph at 3x width. Each color channel (R, G, B) contains the coverage for the corresponding LCD subpixel. The texture format is `MEL_FORMAT_RGBA8_UNORM` (same as SDF/MSDF — NOT sRGB, since the values are coverage, not color).

```c
Mel_Font_Subpixel_Handle sub = mel_font_subpixel_create(desc,
    .size = 14.0f,
    .subpixel_order = MEL_SUBPIXEL_RGB,    // or BGR for some displays
);
```

### Shader

The subpixel shader samples each channel independently and multiplies with the text color per-channel:

```
float r = texture(atlas, uv_for_red_subpixel).r;
float g = texture(atlas, uv_for_green_subpixel).g;
float b = texture(atlas, uv_for_blue_subpixel).b;
output.rgb = text_color.rgb * vec3(r, g, b);
output.a = max(r, max(g, b)) * text_color.a;
```

This requires the quad's UVs to account for the 3x width. Each glyph's UV region in the atlas is 3x wider than the display quad.

### Platform Dependency

Subpixel rendering depends on knowing the display's subpixel layout (RGB, BGR, VRGB, VBGR). This is platform-specific:
- macOS: deprecated ClearType since retina displays made it unnecessary
- Windows: `GetFontSmoothing()` and `CLEARTYPE_QUALITY`
- Linux: fontconfig `rgba` setting

The creation function takes `.subpixel_order` so the caller can query the platform and pass it in. The font system doesn't query the platform itself.

### When to Use

Subpixel rendering is most valuable for small text on non-retina LCD displays (desktop tools, database clients, IDE-like apps). On high-DPI or OLED displays it provides no benefit and can cause color fringing. The technique coexists alongside atlas/SDF/MSDF — the caller picks the right technique for their display context.

---

## Subpixel Positioning

Separate from subpixel *rendering*. Subpixel positioning places glyph quads at fractional pixel coordinates for tighter, more even spacing.

### SDF/MSDF

Get this for free. Quads are placed at arbitrary floating-point positions. The shader's bilinear sampling handles fractional positions naturally.

### Atlas

Atlas fonts are pixel-aligned — placing them at fractional positions causes blur (bilinear sampling between texels). Two solutions:

**Option A: Quantized subpixel offsets.** Pre-rasterize each glyph at N subpixel x-offsets (typically 4: 0.0, 0.25, 0.5, 0.75). The atlas stores N versions per glyph. At render time, quantize the glyph's fractional x position to the nearest offset and select the corresponding atlas entry.

```c
Mel_Font_Atlas_Handle atlas = mel_font_atlas_create(desc,
    .size = 14.0f,
    .subpixel_steps = 4,       // 4 horizontal offsets per glyph
);
```

This multiplies atlas storage by N. For 96 ASCII glyphs × 4 = 384 atlas entries. Reasonable.

**Option B: Snap to integer.** Round all glyph positions to integer coordinates. No fractional positioning, no blur. Trading spacing quality for sharpness. This is what the pixel font technique does.

The technique entry's glyph lookup path must handle the subpixel selection: `glyph_index * subpixel_count + subpixel_offset_index`.

---

## Pixel Font Technique

A dedicated technique for pixel art games. Glyphs are snapped to integer pixel boundaries. No filtering, no subpixel positioning, no scaling.

### Architecture Level

New technique at Level 2: `font.pixel.c`. Created like any other technique:

```c
Mel_Font_Pixel_Handle pixel = mel_font_pixel_create(desc, .size = 8.0f);
```

### Behavior

- Size normalization is integer-only. If `.size = 8` and bake size is 8, scale is 1.0. If `.size = 16`, scale is 2 (integer doubling — nearest-neighbor upscale). Non-integer scales are rounded to the nearest integer multiplier.
- Glyph positions are snapped to integer coordinates before quad generation.
- Atlas texture uses nearest-neighbor (point sampling), not linear filtering.
- The sampler is technique-specific — different from SDF/MSDF which require linear filtering.

### Material

Pixel fonts use only `base_color`. SDF effects are not applicable (no distance field). The material profile is `"pixel"`.

### Use Case

Pixel art games (Street Carlos), retro-style UIs, terminal emulators, any context where crisp pixel-aligned text is desired. Coexists alongside SDF/MSDF for non-pixel text in the same app (e.g. pixel game world text + smooth HUD text).

---

## 3D Text / Mesh Text

Text as 3D geometry in world space. Two approaches serve different needs.

### Approach A: Billboarded Quads

Text rendered as 2D quads in 3D space, always facing the camera. This uses the existing text pipeline entirely — the only change is the transform.

The text pass already produces positioned quads. A 3D text component provides a world-space transform:

```c
ecs_set(world, label, Mel_Transform_3D, { .pos = { 10, 5, 0 } });
ecs_set(world, label, Mel_Text_SDF_3D, {
    .font = sdf,
    .text = S8("Damage: 50"),
    .size = 0.5f,               // 0.5 world units tall
    .billboard = true,
});
```

The Source's `sync` writes the entity's world-space transform into the Manager. The Pipeline projects it through the camera and renders glyph quads as always.

For non-billboard 3D text (e.g. text painted on a wall), the Source writes the entity's full 3D transform into the Manager. The Pipeline applies per-glyph vertex transformation during `draw`.

### Approach B: Extruded Mesh Text

Glyph outlines from the font descriptor, triangulated into 2D meshes, optionally extruded into 3D geometry. This produces mesh data, not text quads — it feeds the mesh pass, not the text pass.

Architecture level: new technique at Level 2: `font.mesh.c`.

```c
Mel_Font_Mesh_Handle mesh_font = mel_font_mesh_create(desc,
    .extrude_depth = 0.5f,      // 0 = flat 2D mesh, >0 = extruded 3D
    .bevel = 0.02f,             // edge bevel for 3D text
);
```

The technique entry stores triangulated mesh data per glyph (vertices + indices). The Source writes mesh geometry into the Manager — these are regular 3D mesh objects, not text glyph buffer entries.

**Glyph outline access**: `stbtt_GetGlyphShape()` returns bezier control points per glyph. The mesh technique triangulates these using ear-clipping or constrained Delaunay triangulation (CDT). Extrusion duplicates the front face, offsets it, and generates side walls. Bevel adds chamfered edges.

**Material**: mesh text uses mesh Material Bases (PBR or unlit), not text Material Bases. The Manager objects use a mesh `material_base_id`, not a text one. The font system's text Material Bases are irrelevant here — this is geometry.

---

## Color Fonts

Color fonts contain embedded color glyph data — used primarily for emoji but also for decorative/chromatic typefaces.

### Font Table Formats

OpenType defines several color glyph formats. Each requires different handling:

- **COLR/CPAL**: Vector-based. Each glyph is a stack of layers, each layer referencing a standard glyph outline + a color from the CPAL palette. The loader extracts layer info. The technique rasterizes each layer and composites them into an RGBA atlas entry.

- **CBDT/CBLC** (or **sbix** on Apple): Embedded bitmap images (PNG). The loader extracts pre-made images at specific sizes. The technique copies them directly into the atlas. No rasterization needed — just atlas packing.

- **SVG**: Embedded SVG documents per glyph. Requires an SVG rasterizer (nanosvg or similar) to convert to bitmap. Highest quality but heaviest dependency.

### Architecture Level

Color fonts are a new technique at Level 2: `font.color.c`. The descriptor reports which color tables are available:

```c
struct Mel_Font_Desc {
    // ... existing fields ...
    u32 color_format;       // MEL_FONT_COLOR_NONE, _COLR, _CBDT, _SBIX, _SVG
};
```

The technique is created specifying which color format to prefer:

```c
Mel_Font_Color_Handle emoji = mel_font_color_create(desc,
    .size = 32.0f,
    .preferred_format = MEL_FONT_COLOR_CBDT,    // prefer bitmap if available
);
```

### Rendering

Color glyph atlas entries are RGBA (`MEL_FORMAT_RGBA8_SRGB` — actual color, not distance fields). The glyph entry's `mode` distinguishes color glyphs from SDF/atlas:

```
mode == MEL_TEXT_RENDER_COLOR:
    output = texture(atlas, uv);    // direct RGBA sample, no SDF math
```

Color glyphs ignore all SDF material params (edge, softness, outline, shadow, glow). The `base_color` material param tints the color glyph (multiplicative).

### Fallback Chain Interaction

Color fonts typically only contain emoji/symbol glyphs, not Latin text. A font stack combines a text font with a color font:

```c
Mel_Font_Stack_Handle stack = mel_font_stack_create(
    .fonts = (void*[]){ sdf_latin, color_emoji },
    .count = 2,
);
```

This is a cross-technique stack (SDF + color). The resolve stage (pipeline stage 3) checks each font in order. When it hits an emoji codepoint, the SDF font doesn't have it, the color font does. The glyph buffer entry for that glyph gets `mode = MEL_TEXT_RENDER_COLOR` and the color font's texture index. Cross-technique stacks are the reason each glyph entry has a `mode` field — glyphs from different fonts in the same text block can use different rendering modes.

---

## Font Metrics Queries

UI layout systems need font metrics beyond what measurement provides. The font system exposes per-technique metric queries:

```c
f32 ascent      = mel_font_sdf_ascent(sdf, .size = 28.0f);
f32 descent     = mel_font_sdf_descent(sdf, .size = 28.0f);
f32 line_height = mel_font_sdf_line_height(sdf, .size = 28.0f);
f32 x_height    = mel_font_sdf_x_height(sdf, .size = 28.0f);
f32 cap_height  = mel_font_sdf_cap_height(sdf, .size = 28.0f);
f32 em_size     = mel_font_sdf_em_size(sdf, .size = 28.0f);
```

All values are size-normalized (same as `.size` — divide by bake size internally). The x-height and cap-height are derived from the bounding boxes of 'x' and 'H' in the glyph data (computed once at technique creation and cached).

The same queries exist for every technique (`mel_font_atlas_ascent`, `mel_font_msdf_ascent`, etc.). Font stacks forward to the primary (first) font in the chain.

These metrics are essential for: baseline alignment between different fonts, line spacing calculations, cursor positioning in text editors, vertical centering of text within UI elements.

---

## Accessibility

### Screen Reader Integration

ECS text components store `.text` as `str8`. Screen readers can query entity text content through the ECS world. The font system does not directly integrate with OS accessibility APIs — that's the application layer's responsibility. But the text data must be *available* for extraction, which it is: the ECS stores the logical text, the font system only consumes it for rendering.

For rich text, the markup-stripped plain text must also be extractable. The parse stage (stage 1) should produce both the `Mel_Text_Run` array and a plain-text `str8` without markup tags.

### Dynamic Text Scaling

Users may need larger text for readability. This is an application-level concern: the app multiplies `.size` by a user preference scale factor before passing it to draw calls. The font system handles arbitrary sizes naturally (SDF/MSDF scale without loss).

For atlas fonts, scaling produces pixel artifacts. If the user's text scale pushes an atlas font far from its bake size, the application should switch to an SDF/MSDF technique handle — or create a new atlas at the desired size. The material backend system can automate this: if the atlas backend detects a size mismatch beyond a threshold, it yields to the SDF backend.

### High Contrast Mode

A material template override. The application creates a "high_contrast" text material template with:
- Maximum edge sharpness (edge = 0.5, softness = 0.0)
- Strong outline (outline = 0.2)
- High-contrast color (white on black, or vice versa)

Material inheritance handles this: override the base template and all instances that inherit from it switch to high-contrast appearance. No per-label changes needed.

---

## Atlas Packing Strategies

All technique handles that produce a texture atlas need a packing algorithm. The choice of algorithm affects atlas utilization, creation speed, and dynamic atlas behavior.

### Static Atlas (creation time)

Used when the full glyph set is known at creation time (the common case).

- **Shelf packing**: Simple. Pack glyphs left-to-right in rows. When a row is full, start a new row. Wastes vertical space when glyph heights vary. Good enough for most Latin charsets where glyphs are similar height.
- **Skyline packing**: Better utilization. Maintains a "skyline" of the highest point in each column. Places each glyph at the lowest available position. stb_rect_pack uses this algorithm.
- **Rectangle packing (stb_rect_pack)**: Best utilization. Sort glyphs by height, pack with skyline bottom-left. Already vendored (stb_truetype depends on it). This is the default choice.

### Dynamic Atlas (runtime)

Used when `.dynamic = true`. Must support incremental insertion without repacking existing glyphs (their UVs are already in use).

- **Shelf packing with growth**: Shelves are append-only. When the atlas is full, create a new atlas page (multi-page atlas) or resize (copy + expand).
- **Free rectangle tracking**: Maintain a list of free rectangles. On insertion, find the best-fit free rect, split it. On atlas resize, the new space is one large free rectangle. More complex but better utilization than shelves.

### Multi-Page Atlas

When a single texture can't hold all glyphs (very large charsets, small atlas size), the technique entry uses multiple texture pages:

```c
struct Mel_Font_Atlas_Entry {
    Mel_Font_Glyph* glyphs;
    u32 glyph_count;
    Mel_Gpu_Texture* pages;         // array of textures
    u32 page_count;
    // ...
};
```

Each glyph's UV data includes a `page_index`. The glyph buffer entry references a specific page's texture index (via the global texture table for bindless, or a texture slot for non-bindless tiers). The Pipeline sorts draws by texture to minimize switches.

---

## Features Roadmap

Everything below is designed in this document. The tiers indicate implementation order, not design completeness. Every feature has a defined architectural slot — adding any of them should not require reworking existing systems.

### Tier 1: Foundation

The core architecture that everything else builds on:

- Three-level architecture (loader → descriptor → technique handle)
- Atlas, SDF, MSDF techniques with module-static pools
- Size normalization in draw functions
- Async loading via job/fiber system
- Material system integration (text material family, dual API)
- Texture format field on `Mel_Gpu_Texture_Opt`
- Basic measurement and font metrics queries
- Kerning
- Pipeline stages defined with `Mel_Text_Layout_Result` intermediate type (shortcut path only — stages 1-5 are passthrough)

### Tier 2: Text Features

Features that make text actually useful for games and apps:

- SDF effects (shadow, glow) — shader additions, material params already reserved in `params1`
- Text layout (wrapping, alignment, line breaking) — pipeline stage 4 implementation
- Font fallback chains (`Mel_Font_Stack_Handle`) — pipeline stage 3 implementation
- Rich text parsing — pipeline stage 1 implementation, produces `Mel_Text_Run` arrays
- Per-character effects (wave, shake, typewriter, fade, rainbow) — pipeline stage 5 implementation
- Curved / path text — stage 5 effect using `rotation` field on positioned glyphs
- Pixel font technique — `font.pixel.c`, integer-only scaling, nearest-neighbor sampling

### Tier 3: Advanced Rendering

Features that expand what text can be:

- Dynamic atlas / glyph-on-demand — runtime rasterization, shelf packing with growth, changes descriptor lifetime rules
- Subpixel rendering (ClearType) — `font.subpixel.c`, 3x horizontal rasterization, per-channel shader
- Subpixel positioning for atlas fonts — multi-offset glyph variants
- 3D text: billboarded quads (text pass + 3D transform) and extruded mesh text (`font.mesh.c` → mesh pass)
- Color fonts (COLR/CPAL, CBDT/sbix, SVG) — `font.color.c`, RGBA atlas, direct sampling
- Multi-page atlas — array of textures per technique entry, page_index per glyph

### Tier 4: Internationalization

Features for proper global text support:

- Text shaping (HarfBuzz) — pipeline stage 2 replacement, pluggable shaper interface
- Bidirectional text (UAX #9) — part of shaping, per-run direction
- Vertical text layout — CJK top-to-bottom, direction parameter on layout, vertical metrics from font
- BMFont loader — new Level 0 loader for pre-made bitmap font descriptions
- OpenType loader — full OpenType table parsing beyond what stb_truetype provides

### Tier 5: Platform Integration

Features that connect text to the broader system:

- Accessibility — screen reader text extraction, dynamic scaling, high contrast material override
- Cross-technique font stacks — mixed SDF + color stacks for emoji in text
- Platform subpixel order detection — OS-specific queries for ClearType configuration

---

## References

These engines and libraries informed the design. Specific aspects referenced:

- **Unity TextMeshPro**: SDF-based text rendering with material-driven styling. Rich text tags for inline formatting. Per-character vertex modifiers for effects (wave, shake, etc). Multi-atlas fallback fonts. SDF shader effects (shadow, glow, outline, bevel) all through material parameters. *Referenced for*: material integration, SDF effects, rich text tags, per-char effects, the proof that SDF + materials is the right architecture for game text.

- **Godot**: `RichTextLabel` with BBCode markup. `DynamicFont` with runtime SDF generation. MSDF support added in 4.0. Font fallback chains with priority. Glyph caching with LRU eviction for dynamic fonts. Vertical text support for CJK. Color emoji through system fonts. *Referenced for*: rich text markup, dynamic font generation, fallback chains, vertical text, color fonts.

- **Unreal Engine (Slate)**: Composite fonts (mapping Unicode ranges to different font faces — exactly our font stack concept). Rich text decorators (extensible inline formatting, not limited to predefined tags). SDF rendering in Slate UI. HarfBuzz integration for text shaping. *Referenced for*: composite fonts as the fallback chain model, extensible rich text, shaping integration.

- **Bevy**: cosmic-text for text shaping and layout (HarfBuzz + rusttype underneath). ab_glyph for rasterization. `TextBundle` with styled sections (each section = a run with its own font/color/size). Separation of shaping, layout, and rendering into distinct stages. *Referenced for*: the "text run" concept, pipeline stage separation, shaping as pluggable.

- **Dear ImGui**: Single atlas per font, no SDF. But excellent API ergonomics — `PushFont()` / `PopFont()` for font switching, `TextColored()` for quick styling. Demonstrates that a simple immediate-mode text API covers 90% of use cases. Custom glyph ranges for CJK partial loading. *Referenced for*: API ergonomics, the value of keeping the simple path simple, glyph range specification.

- **BGFX font example**: TrueType and distance field rendering. Text buffer manager that batches text into GPU buffers. Multi-texture atlas support. *Referenced for*: GPU buffer management pattern for text batching, multi-page atlas.

- **Slug (Eric Lengyel)**: Band-limited SDF rendering directly from glyph outlines without pre-baking an atlas. GPU-evaluated bezier curves. Represents the theoretical ideal — infinite resolution from outlines. *Referenced for*: awareness of atlas-free approaches as future research direction.

- **HarfBuzz**: The industry-standard text shaping library. Handles complex scripts (Arabic, Devanagari, Thai), ligatures, contextual substitution, bidirectional reordering. Our shaper interface is designed to wrap HarfBuzz when available. *Referenced for*: shaper interface design, understanding what shaping actually needs from a font descriptor.

- **FreeType**: Comprehensive font rasterizer. Subpixel rendering (ClearType-style LCD filtering), hinting, auto-hinter. We use stb_truetype instead for simplicity, but FreeType's subpixel approach informs our subpixel technique design. *Referenced for*: subpixel rendering technique, the distinction between subpixel rendering and subpixel positioning.
