# Probe — Material System

## Purpose

This probe shows how Melody's material subsystem should feel at the app level.

The goal is:
- simple for sprites and UI
- coherent for normal 3D
- extensible for custom game materials
- still valid for deferred, mesh-shader, and GPU-driven paths

The important rule is that the top-level architecture does not change:

```text
sources -> views -> techniques -> frame recipe -> compiled frame plan -> graph
```

Materials slot into that stack as the semantic contract for shading and surface
behavior.

---

## Example 1 — Small 2D Game

### What The Game Wants

- one world sprite view
- one HUD overlay
- engine-default sprite and UI materials
- per-instance tint overrides

### Init

```c
static Mel_Material_Template_Handle world_sprite_template;
static Mel_Material_Template_Handle hud_panel_template;

static Mel_Material_Instance_Handle player_material;
static Mel_Material_Instance_Handle enemy_material;
static Mel_Material_Instance_Handle hud_panel_material;

void app_init(void)
{
    world_sprite_template = mel_material_template_create(&(Mel_Material_Template_Desc){
        .name = S8("world_sprite"),
        .family = MEL_MATERIAL_FAMILY_SPRITE,
        .profile = S8("sprite.unlit"),
        .textures = {
            [MEL_SPRITE_TEX_ALBEDO] = atlas_texture,
        },
    });

    hud_panel_template = mel_material_template_create(&(Mel_Material_Template_Desc){
        .name = S8("hud_panel"),
        .family = MEL_MATERIAL_FAMILY_UI,
        .profile = S8("ui.panel"),
    });

    player_material = mel_material_instance_create(world_sprite_template);
    enemy_material = mel_material_instance_create(world_sprite_template);
    hud_panel_material = mel_material_instance_create(hud_panel_template);

    mel_material_instance_set_vec4(player_material, S8("tint"), player_color);
    mel_material_instance_set_vec4(enemy_material,  S8("tint"), enemy_color);
}
```

### Source Data

```c
typedef struct {
    Mel_Vec2 pos;
    Mel_Vec2 size;
    Mel_Rect uv;
    Mel_Material_Instance_Handle material;
    f32 depth;
} Game_Sprite_Entry;
```

This is the intended starting contract for CPU-fed and retained paths:
- source entries carry `material_instance`
- techniques resolve the concrete backend later
- the app does not need to micromanage shader variants

### What Resolves

On a desktop build:

```text
technique request: sprite
resolved technique: sprite
resolved material backend:
    sprite.unlit.bindless
    ui.panel.bindless
```

On a web build:

```text
technique request: sprite
resolved technique: sprite
resolved material backend:
    sprite.unlit.atlased_web
    ui.panel.portable_web
```

### Why This Is Good

- the game still just pushes sprite entries
- the material contract is explicit instead of hidden in the sprite pass
- web/mobile fallback is centralized

---

## Example 2 — Ordinary 3D Game

### What The Game Wants

- one main view
- one minimap
- one HUD
- engine-native `surface` materials
- same authored materials on desktop and mobile

### Authoring Shape

```c
static Mel_Material_Template_Handle hero_armor;
static Mel_Material_Template_Handle terrain_rock;
static Mel_Material_Template_Handle foliage_cutout;

void load_materials(void)
{
    hero_armor = mel_material_template_create(&(Mel_Material_Template_Desc){
        .name = S8("hero_armor"),
        .family = MEL_MATERIAL_FAMILY_SURFACE,
        .profile = S8("surface.standard"),
        .features = {
            S8("normal_map"),
            S8("metal_rough"),
        },
        .textures = {
            [MEL_SURFACE_TEX_BASE_COLOR] = tex_hero_base,
            [MEL_SURFACE_TEX_NORMAL] = tex_hero_normal,
            [MEL_SURFACE_TEX_METAL_ROUGH] = tex_hero_mr,
        },
    });

    foliage_cutout = mel_material_template_create(&(Mel_Material_Template_Desc){
        .name = S8("foliage_cutout"),
        .family = MEL_MATERIAL_FAMILY_SURFACE,
        .profile = S8("surface.foliage"),
        .features = {
            S8("alpha_test"),
            S8("double_sided"),
        },
    });
}
```

### Desktop Resolution

```text
view: main
requested technique family: mesh
resolved technique: mesh.deferred

resolved material backends:
    hero_armor     -> surface.standard.deferred.gbuffer
    terrain_rock   -> surface.standard.deferred.gbuffer
    foliage_cutout -> surface.foliage.forward_cutout
```

### Mobile Resolution

```text
view: main
requested technique family: mesh
resolved technique: mesh.forward

resolved material backends:
    hero_armor     -> surface.standard.forward.mobile
    terrain_rock   -> surface.standard.forward.mobile
    foliage_cutout -> surface.foliage.forward_cutout.mobile
```

### Important Part

The view, recipe, and source model stay the same.
Only technique and material backend resolution differ.

The source contract also stays simple:
- ordinary mesh instances carry `material_instance`
- the engine resolves backend + bucket classification under the selected
  technique path

---

## Example 3 — Strict Vs Permissive Policy

### Strict Hero Material

```c
Mel_Material_Template_Handle hero_face = mel_material_template_create(
    &(Mel_Material_Template_Desc){
        .name = S8("hero_face"),
        .family = MEL_MATERIAL_FAMILY_SURFACE,
        .profile = S8("surface.skin"),
        .fallback_policy = MEL_MATERIAL_FALLBACK_STRICT,
    });
```

If subsurface-capable backends are unavailable:

```text
hero_face:
    requested profile: surface.skin
    allowed fallback: strict
    result: compile failure
```

### Permissive Crowd Material

```c
Mel_Material_Template_Handle crowd_shirt = mel_material_template_create(
    &(Mel_Material_Template_Desc){
        .name = S8("crowd_shirt"),
        .family = MEL_MATERIAL_FAMILY_SURFACE,
        .profile = S8("surface.standard"),
        .fallback_policy = MEL_MATERIAL_FALLBACK_ALLOW,
    });
```

On a constrained device:

```text
crowd_shirt:
    surface.standard
    -> surface.standard.forward.mobile
    -> surface.simple_lit
    selected: surface.simple_lit
```

This is the kind of policy choice that belongs in the material subsystem, not
scattered across passes.

---

## Example 4 — Custom Game Family

### What The Game Wants

- a toon shading family
- works with the built-in mesh technique
- custom fallback to ordinary unlit if needed

### Registration

```c
mel_material_family_register(&(Mel_Material_Family_Desc){
    .name = S8("toon.surface"),
    .compatible_source_schemas = {
        MEL_SCHEMA_MESH_INSTANCE,
    },
    .compatible_techniques = {
        MEL_TECHNIQUE_MESH,
    },
});

mel_material_backend_register(&(Mel_Material_Backend_Desc){
    .family = mel_material_family(S8("toon.surface")),
    .name = S8("toon.surface.forward"),
    .technique_family = MEL_TECHNIQUE_MESH,
});

mel_material_backend_register(&(Mel_Material_Backend_Desc){
    .family = mel_material_family(S8("toon.surface")),
    .name = S8("toon.surface.visibility_buffer"),
    .technique_family = MEL_TECHNIQUE_MESH,
});
```

### Template

```c
Mel_Material_Template_Handle boss_toon = mel_material_template_create(
    &(Mel_Material_Template_Desc){
        .name = S8("boss_toon"),
        .family = mel_material_family(S8("toon.surface")),
        .profile = S8("toon.surface.outlined"),
    });
```

### Resolution

```text
desktop high-end:
    mesh -> mesh_shader.visibility_buffer
    boss_toon -> toon.surface.visibility_buffer

mobile:
    mesh -> mesh.forward
    boss_toon -> toon.surface.forward

worst-case fallback:
    boss_toon -> surface.unlit
```

The game added a new family without forking the engine's frame architecture.

---

## Example 5 — GPU-Driven Material Table

### What The Game Wants

- meshlet scene
- GPU-owned instance and material tables
- visibility-buffer shading

### Sources

```c
static Mel_Source_Handle instance_db;
static Mel_Source_Handle meshlet_db;
static Mel_Source_Handle material_db;
```

### View Wiring

```c
mel_view_attach_source(main_view, instance_db);
mel_view_attach_source(main_view, meshlet_db);
mel_view_attach_source(main_view, material_db);

mel_frame_recipe_use_technique(recipe, main_view, MEL_TECHNIQUE_MESH);
```

### Resolution

```text
requested technique: mesh
resolved technique: mesh_shader.visibility_buffer

material source schema: MATERIAL_TABLE
resolved material backends:
    surface.standard.visibility_buffer
    surface.decal.visibility_buffer
```

### What The Plan Likely Compiles

```text
async compute: cull visible instances
graphics: visibility fill
graphics: material resolve from material table
graphics: transparent forward fallback
graphics: postprocess
```

This path still uses the same top-level source/view/recipe/plan model.

The key difference from smaller paths is just source integration:
- CPU/retained records use `material_instance`
- GPU-driven records use `material_id` / material-table sources

The material subsystem should support both without changing the app-level frame
model.

---

## Example 6 — Compile Vs Refresh

### Refresh-Only Change

```c
mel_material_instance_set_vec4(player_material, S8("tint"), flash_color);
mel_material_instance_set_f32(player_material, S8("emissive_boost"), 2.0f);

mel_frame_plan_refresh(plan, &(Mel_Frame_Plan_Refresh_Opt){0});
```

Why refresh only:
- same family
- same profile
- same backend class
- same slot layout

### Recompile Change

```c
mel_material_template_set_feature(hero_armor, S8("clear_coat"), true);
```

Why recompile may be needed:
- static feature set changed
- backend may switch
- deferred compatibility may change

### Source-Shape Escalation

```c
material_db = rebuild_material_source_with_new_layout(...);
```

Why topology may be dirty:
- source schema/layout changed
- technique/material compatibility must be rechecked

This matches the existing compiled-frame lifecycle:
- authored structure
- compiled topology
- refresh-time values
- source-shape changes

---

## What The Probe Is Trying To Protect

If Melody follows this shape:
- sprite/UI materials stay easy
- 3D materials stay coherent
- custom game materials stay first-class
- capability and fallback policy stay centralized
- future deferred and mesh-shader paths do not require a second architecture

That is the real success condition for the material subsystem.
