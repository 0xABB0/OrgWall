# Asset System

## Philosophy

**Each asset type owns its own loading.** No unified asset manager.

`mel_load_texture` lives in the texture module. `mel_load_font_atlas` lives in the
font module. Adding a new asset type = adding a new pool + a new `mel_load_*` in a
new file. No central bottleneck to edit.

Each pool is a module-level static. The `_opt` pattern allows overriding with a
custom pool instance for testing or multi-instance scenarios.

## Status

This document is target architecture (vNext).

Current state:
- `mel_texture_pool_load` is synchronous today.
- `mel_texture_pool_tick` contains TODO async integration.
- only `font.atlas.*` is currently implemented.

---

## Async-First Loading (Target vNext)

Every `mel_load_*` returns a valid handle **immediately**.

```c
Mel_Texture_Handle h = mel_load_texture(S8("hero.png"));
```

The handle is always safe to use. The pool manages the visual state:

- **LOADING**: pool returns the `tex_loading` fallback when the handle is queried for GPU data
- **FAILED**: pool returns `tex_missing` (magenta/black checkerboard)
- **LOADED**: pool returns the real texture

The game draws with the handle. State transitions happen transparently.
Game code never branches on "is this loaded?"

### The Async Layout Footgun

If UI or layout code needs texture dimensions immediately (e.g., to center a panel), but the texture is still loading, querying its size might return the dimensions of `tex_loading` (e.g., 32x32). When the real 1024x1024 texture finishes loading, the UI will "pop" or remain incorrectly sized.

**Solution: Synchronous Info Read**

When layout matters immediately, use the info variant:
```c
Mel_Texture_Info info;
Mel_Texture_Handle h = mel_load_texture(S8("hero.png"), .info = &info);
```
This synchronously reads *just the file header* via VFS (taking microseconds) to populate `info.width` and `info.height`, while the heavy pixel decoding continues asynchronously in the background.

### Loading Status Queries

Every pool exposes the same query pattern. These use the `_opt` pattern —
pass NULL pool to use the module static.

**Per-asset:**

```c
bool mel_texture_loaded(Mel_Texture_Handle h);
f32  mel_texture_progress(Mel_Texture_Handle h);   // 0.0 → 1.0
void mel_texture_wait(Mel_Texture_Handle h);        // blocks until loaded or failed

bool mel_font_atlas_loaded(Mel_Font_Atlas_Handle h);
bool mel_tileset_loaded(Mel_Tileset_Handle h);
```

Progress tracks the full chain: file read → decode → GPU upload.
Blocking wait uses a VFS ticket (backed by Mel_Io internally).

**Loading gates (groups of assets):**

For loading screens or dependency gates where you need to know "are ALL of
these ready before I proceed":

```c
Mel_Load_Gate gate = mel_load_gate_create();
mel_load_gate_add_texture(&gate, hero_tex);
mel_load_gate_add_texture(&gate, enemy_tex);
mel_load_gate_add_font_atlas(&gate, ui_font);
mel_load_gate_add_tilemap(&gate, level_map);

f32  mel_load_gate_progress(Mel_Load_Gate* gate);  // 0.0 → 1.0 across all assets
bool mel_load_gate_is_loaded(Mel_Load_Gate* gate);  // true when everything is ready
void mel_load_gate_wait(Mel_Load_Gate* gate);        // blocks until all loaded or failed
```

The gate queries individual pools internally. It's a convenience — you could
poll each handle individually, but gates give you aggregate progress for
loading bars and a single "go/no-go" check.

**Typical loading screen pattern:**

```c
static Mel_Load_Gate gate;
static bool level_ready;

void load_level(void) {
    hero_tex  = mel_load_texture(S8("hero.png"));
    enemy_tex = mel_load_texture(S8("enemy.png"));
    level_map = mel_load_tilemap(S8("level1.tmx"));

    gate = mel_load_gate_create();
    mel_load_gate_add_texture(&gate, hero_tex);
    mel_load_gate_add_texture(&gate, enemy_tex);
    mel_load_gate_add_tilemap(&gate, level_map);
}

void loading_tick(Mel_Sim_Ctx* sim, f32 dt, void* user) {
    f32 progress = mel_load_gate_progress(&gate);
    update_loading_bar(progress);

    if (mel_load_gate_is_loaded(&gate)) {
        spawn_level();
        mel_sim_remove_variable(sim, loading_update_h);
        Mel_Sim_Fixed* physics = mel_sim_add_fixed(sim, .fixed_dt = 1.0f / 60.0f);
        mel_sim_fixed_add_update(physics, gameplay_tick);
    }
}
```

### Default Assets

Generated synthetically during `mel_init()` (never loaded from disk):

- **`tex_white`** — 1x1 white pixel. The "zero asset." Default for untextured draws.
- **`tex_missing`** — magenta/black checkerboard. Asset not found.
- **`tex_loading`** — loading indicator. Asset in-flight.

These handles are GUARANTEED valid from `mel_init()` to `mel_shutdown()`.
Stored as module-level statics in the texture pool module.

---

## Per-Type Loading API

```c
Mel_Texture_Handle  mel_load_texture(str8 path, ...);
Mel_Tileset_Handle  mel_load_tileset(str8 path, ...);
Mel_Tilemap_Handle  mel_load_tilemap(str8 path, ...);
```

Each uses the `_opt` pattern. NULL fields = module static:

```c
typedef struct {
    Mel_Texture_Pool* pool;
    Mel_Vfs* vfs;
    Mel_Texture_Info* info;
} Mel_Load_Texture_Opt;

Mel_Texture_Handle mel_load_texture_opt(str8 path, Mel_Load_Texture_Opt);
#define mel_load_texture(path, ...) mel_load_texture_opt((path), (Mel_Load_Texture_Opt){__VA_ARGS__})
```

Each function internally:
1. Checks pool's path→handle cache (deduplication)
2. If cached and alive, returns existing handle
3. Otherwise: creates entry in pool, submits async VFS read, returns handle
4. When VFS read + decode + GPU upload completes: mark LOADED
5. On failure: mark FAILED

### Direct Pool Access

Pools are module statics but **not hidden**:

```c
Mel_Texture_Pool* pool = mel_texture_pool_instance();
Mel_Gpu_Texture* raw = mel_texture_pool_get(pool, handle);
mel_texture_pool_unload(pool, handle);
```

The convenience API (`mel_load_*`) is the 80% path. Direct access is the escape hatch.
Bindless ID queries are target API and will be exposed once bindless table lands.

---

→ Font architecture (descriptor, atlas/SDF/MSDF, adding new techniques): `engine.fonts.md`

---

## Open Questions

1. **Hot-reload integration**: file watcher detects change → which pool gets notified?
   VFS watch → callback with virtual path → pool looks up path in cache → reloads.
   Each pool implements its own reload logic.

3. **Texture atlas packing**: should the texture pool support atlasing (multiple small
   textures packed into one GPU texture)? Or is that a separate `Mel_Texture_Atlas`
   system feeding into the pool? Current code has `texture.atlas.*` as a separate module.
