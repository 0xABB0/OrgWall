# Probe — Compiled Frame Lifecycle

## Purpose

This probe shows how the next rendering milestone should feel in practice.

The key difference from earlier probes is this:

- not every frame should mean "recompile the plan"
- not every render data change should mean "mutate the recipe"

The engine should distinguish between:

- compile
- refresh
- source update

That distinction has to hold for both simple games and modern GPU-driven
renderers.

---

## Mental Model

Each frame should look like:

```text
sim update
-> extraction updates sources
-> frame plan refreshes parameters if needed
-> graph executes
```

Only structural changes should look like:

```text
recipe/view/source-shape change
-> frame plan recompile
-> graph executes
```

---

## Example 1 — Simple 2D Game

### What Changes Every Frame

- player position
- enemy positions
- HUD text
- camera follow offset

### What Rarely Changes

- which views exist
- which techniques those views use
- which swapchain they present to

### App Shape

```c
static Mel_View_Handle world_view;
static Mel_View_Handle hud_view;

static Mel_Source_Handle world_sprites;
static Mel_Source_Handle hud_sprites;
static Mel_Source_Handle hud_text;

static Mel_Frame_Recipe_Handle recipe;
static Mel_Frame_Plan_Handle plan;
```

### Init

```c
void app_init(void)
{
    world_view = mel_view_create(&(Mel_View_Desc){
        .name = S8("world"),
        .camera = &world_camera,
    });

    hud_view = mel_view_create(&(Mel_View_Desc){
        .name = S8("hud"),
        .composition_mode = MEL_VIEW_COMPOSE_ALPHA,
    });

    world_sprites = mel_source_from_render_list(&world_sprite_list, MEL_SCHEMA_SPRITE);
    hud_sprites   = mel_source_from_render_list(&hud_sprite_list,   MEL_SCHEMA_SPRITE);
    hud_text      = mel_source_from_render_list(&hud_text_list,     MEL_SCHEMA_TEXT);

    mel_view_attach_source(world_view, world_sprites);
    mel_view_attach_source(hud_view, hud_sprites);
    mel_view_attach_source(hud_view, hud_text);

    recipe = mel_frame_recipe_create(S8("game"));
    plan   = mel_frame_plan_create(S8("game"));

    mel_frame_recipe_use_technique(recipe, world_view, MEL_TECHNIQUE_SPRITE);
    mel_frame_recipe_use_technique(recipe, hud_view,   MEL_TECHNIQUE_SPRITE);
    mel_frame_recipe_use_technique(recipe, hud_view,   MEL_TECHNIQUE_TEXT);

    mel_frame_recipe_present(recipe, world_view, game_swapchain);
    mel_frame_recipe_overlay(recipe, hud_view, game_swapchain);

    mel_frame_plan_compile(plan, recipe, ...);
}
```

### Per Frame

```c
void frame_tick(void)
{
    game_sim_tick();

    extract_world_sprites(&world_sprite_list, sim_state);
    extract_hud(&hud_sprite_list, &hud_text_list, sim_state);

    world_camera.position = camera_follow(sim_state.player_pos);

    mel_frame_plan_refresh(plan, &(Mel_Frame_Refresh_Desc){
        .views = MEL_REFRESH_ALL_VIEWS,
        .sources = MEL_REFRESH_ALL_SOURCES,
    });
}
```

### What Does Not Happen

- no recipe rebuild
- no graph rebuild
- no technique re-resolution

### What Triggers Recompile

Examples:

- enabling a minimap view
- disabling the text technique
- changing output topology from direct-present to offscreen + postprocess

---

## Example 2 — 3D Game With Tools

### Views

- `main_world`
- `minimap`
- `hud`
- `tools_debug`

### Sources

- retained mesh instances
- retained lights
- sprite HUD source
- text HUD source
- debug line source

### Per-Frame Flow

```c
void frame_tick(void)
{
    sim_tick();

    extract_mesh_transforms(mesh_source, world_state);
    extract_lights(light_source, world_state);
    extract_debug(debug_source, world_state);
    extract_hud(hud_sprite_source, hud_text_source, ui_state);

    update_main_camera(&main_camera, world_state);
    update_minimap_camera(&minimap_camera, world_state);

    mel_frame_plan_refresh(plan, &(Mel_Frame_Refresh_Desc){
        .refresh_cameras = true,
        .refresh_viewports = true,
        .refresh_technique_params = true,
    });
}
```

### When Recompile Happens

- enabling photo mode
- switching the main view from forward mesh to deferred mesh
- changing shadow technique variant class
- attaching a new postprocess chain

### Why This Is Better

The engine is no longer conflating:

- scene data mutation
- camera mutation
- frame topology mutation

That is the minimum required for serious 3D rendering to stay fast and simple.

---

## Example 3 — GPU-Driven Meshlet Renderer

This is the stress test.

The top-level app model should still be:

- sources
- views
- recipe
- plan

Only the source kinds and technique variant become more advanced.

### Sources

```c
static Mel_Source_Handle meshlet_db;
static Mel_Source_Handle instance_db;
static Mel_Source_Handle material_db;
static Mel_Source_Handle cull_results;
```

### Recipe

```c
mel_view_attach_source(main_view, meshlet_db);
mel_view_attach_source(main_view, instance_db);
mel_view_attach_source(main_view, material_db);
mel_view_attach_source(main_view, cull_results);

mel_frame_recipe_use_technique(recipe, main_view, MEL_TECHNIQUE_MESH);
```

### Resolution

```text
requested: MEL_TECHNIQUE_MESH
resolved:  mesh_shader.visibility_buffer
fallback:  mesh.indirect
fallback:  mesh.forward
```

### Per Frame

```c
void frame_tick(void)
{
    sim_tick();

    upload_instance_updates(instance_db, world_state);

    mel_frame_plan_refresh(plan, &(Mel_Frame_Refresh_Desc){
        .refresh_cameras = true,
        .refresh_technique_params = true,
    });
}
```

### What Compile Handles

- pass topology
- async compute participation
- intermediate target allocation
- concrete variant selection

### What Refresh Handles

- camera matrices
- LOD distances
- culling constants
- per-frame descriptor offsets
- dynamic resolution values

### Why This Matters

If Melody gets this right, modern rendering becomes:

- more advanced internally
- not more complex at the app-facing level

That is the real architectural win.

---

## Stage Relationship

`stage.2d` and `stage.3d` should sit above this lifecycle, not replace it.

They should:

- author default recipes
- own default views
- own standard source attachment patterns
- trigger compile only on structural changes
- trigger refresh on normal frame changes

That keeps the no-boilerplate path honest.

It is not a second rendering architecture.

---

## Success Signal

This milestone is correct when a game can say:

```text
I changed my camera and sprite contents.
Refresh the frame.
```

and Melody does not secretly:

```text
tear down and rebuild the plan again.
```
