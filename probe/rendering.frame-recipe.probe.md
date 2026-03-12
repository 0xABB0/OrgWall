# Probe — Rendering With Sources, Views, Techniques, Recipes

## Purpose

This probe shows how the rendering architecture should feel at the app level.

The goal is:
- simple when drawing a few sprites
- scalable to a big 3D game
- still coherent when using very modern GPU-driven rendering

The stack used in these examples is:

1. **sources**
   Typed render inputs such as sprites, text, meshes, meshlets, lights, debug
   primitives, GPU-written databases, or render targets.

2. **views**
   Visibility and composition intent.

3. **techniques**
   Engine-native renderers such as `sprite`, `text`, `mesh.forward`,
   `mesh.deferred`, `mesh_shader`, `shadow`, `debug`, `imgui`, `postprocess`.

4. **frame recipe**
   The authored plan that binds views, techniques, and swapchains into a frame.

5. **execution graph**
   The compiled pass/resource DAG that actually runs.

The app mostly owns:
- simulation
- extraction into sources
- cameras
- view setup
- recipe setup

The engine mostly owns:
- technique selection
- target allocation
- graph construction
- pass ordering
- barriers
- presentation

---

## Example 1: Simple 2D Game

### What The Game Wants

- one window
- one world camera
- one HUD overlay
- sprites and text
- no custom passes

### Init-Time Shape

```c
static Mel_Window_Handle game_window;
static Mel_Swapchain_Handle game_swapchain;

static Mel_View_Handle world_view;
static Mel_View_Handle hud_view;

static Mel_Frame_Recipe_Handle frame_recipe;

static Mel_Render_List world_sprites;
static Mel_Render_List hud_sprites;
static Mel_Render_List hud_text;

static Mel_Source_Handle world_sprite_source;
static Mel_Source_Handle hud_sprite_source;
static Mel_Source_Handle hud_text_source;

static Mel_Camera world_camera;

void app_init(void)
{
    game_window = mel_window_create(S8("Game"), 1280, 720);
    game_swapchain = mel_window_swapchain(game_window);

    mel_render_list_init(&world_sprites,
        .name = S8("world_sprites"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .mode = MEL_RENDER_LIST_EPHEMERAL);

    mel_render_list_init(&hud_sprites,
        .name = S8("hud_sprites"),
        .entry_stride = sizeof(Mel_Sprite_Entry),
        .mode = MEL_RENDER_LIST_EPHEMERAL);

    mel_render_list_init(&hud_text,
        .name = S8("hud_text"),
        .entry_stride = sizeof(Mel_Text_Entry),
        .mode = MEL_RENDER_LIST_EPHEMERAL);

    world_view = mel_view_create(&(Mel_View_Desc){
        .name = S8("world"),
        .camera = &world_camera,
        .clear_color_enabled = true,
        .clear_color = mel_vec4(0.08f, 0.09f, 0.12f, 1.0f),
    });

    hud_view = mel_view_create(&(Mel_View_Desc){
        .name = S8("hud"),
        .composition_mode = MEL_VIEW_COMPOSE_ALPHA,
    });

    world_sprite_source = mel_source_from_render_list(&world_sprites, MEL_SCHEMA_SPRITE);
    hud_sprite_source   = mel_source_from_render_list(&hud_sprites, MEL_SCHEMA_SPRITE);
    hud_text_source     = mel_source_from_render_list(&hud_text, MEL_SCHEMA_TEXT);

    mel_view_attach_source(world_view, world_sprite_source);
    mel_view_attach_source(hud_view, hud_sprite_source);
    mel_view_attach_source(hud_view, hud_text_source);

    frame_recipe = mel_frame_recipe_create(S8("game"));

    mel_frame_recipe_use_technique(frame_recipe, world_view, MEL_TECHNIQUE_SPRITE);
    mel_frame_recipe_use_technique(frame_recipe, hud_view,   MEL_TECHNIQUE_SPRITE);
    mel_frame_recipe_use_technique(frame_recipe, hud_view,   MEL_TECHNIQUE_TEXT);

    mel_frame_recipe_present(frame_recipe, world_view, game_swapchain);
    mel_frame_recipe_overlay(frame_recipe, hud_view, game_swapchain);
}
```

### Per-Frame Shape

```c
void extract_world(Mel_Sim_Ctx* sim, f32 dt, void* user)
{
    push_player_sprite(&world_sprites, player);
    push_enemy_sprites(&world_sprites, enemies);

    push_life_bar(&hud_sprites, player_hp);
    push_score_text(&hud_text, score);
}
```

### What The Engine Likely Compiles

```text
world sprite technique  -> world_color
hud sprite technique    -> window_swapchain
hud text technique      -> window_swapchain
present/composite       world_color -> window_swapchain
```

### Why This Is Good

- the game never creates a sprite pass
- the game never creates a present pass
- the game never deals with barriers or intermediate targets
- if the engine later wants batching, bindless sprites, or a better text path,
  the game code does not change

---

## Example 2: Big 3D Game

### What The Game Wants

- one main gameplay view
- one photo mode view
- one minimap
- one HUD and debug overlay
- two windows: game and tools
- shadows, deferred lighting, decals, transparent objects, postprocess

### Storage

```c
static Mel_Render_List mesh_instances;
static Mel_Render_List skinned_meshes;
static Mel_Render_List decals;
static Mel_Render_List transparent_meshes;
static Mel_Render_List lights;
static Mel_Render_List debug_lines;
static Mel_Render_List hud_sprites;
static Mel_Render_List hud_text;
```

### Sources

```c
static Mel_Source_Handle mesh_source;
static Mel_Source_Handle skinned_mesh_source;
static Mel_Source_Handle decal_source;
static Mel_Source_Handle transparent_mesh_source;
static Mel_Source_Handle light_source;
static Mel_Source_Handle debug_source;
static Mel_Source_Handle hud_sprite_source;
static Mel_Source_Handle hud_text_source;
```

### Views

```c
static Mel_View_Handle main_view;
static Mel_View_Handle photo_view;
static Mel_View_Handle minimap_view;
static Mel_View_Handle hud_view;
static Mel_View_Handle tools_debug_view;
```

```c
main_view = mel_view_create(&(Mel_View_Desc){
    .name = S8("main"),
    .camera = &game_camera,
    .clear_color_enabled = true,
});

photo_view = mel_view_create(&(Mel_View_Desc){
    .name = S8("photo"),
    .camera = &photo_camera,
    .render_scale = 1.0f,
});

minimap_view = mel_view_create(&(Mel_View_Desc){
    .name = S8("minimap"),
    .camera = &minimap_camera,
    .viewport = mel_rect(0.72f, 0.04f, 0.24f, 0.24f),
    .viewport_is_normalized = true,
});

hud_view = mel_view_create(&(Mel_View_Desc){
    .name = S8("hud"),
    .composition_mode = MEL_VIEW_COMPOSE_ALPHA,
});
```

### Recipe

```c
frame_recipe = mel_frame_recipe_create(S8("gameplay"));

mesh_source             = mel_source_from_render_list(&mesh_instances, MEL_SCHEMA_MESH_INSTANCE);
skinned_mesh_source     = mel_source_from_render_list(&skinned_meshes, MEL_SCHEMA_SKINNED_MESH_INSTANCE);
decal_source            = mel_source_from_render_list(&decals, MEL_SCHEMA_DECAL);
transparent_mesh_source = mel_source_from_render_list(&transparent_meshes, MEL_SCHEMA_MESH_INSTANCE);
light_source            = mel_source_from_render_list(&lights, MEL_SCHEMA_LIGHT);
debug_source            = mel_source_from_render_list(&debug_lines, MEL_SCHEMA_DEBUG_LINE);
hud_sprite_source       = mel_source_from_render_list(&hud_sprites, MEL_SCHEMA_SPRITE);
hud_text_source         = mel_source_from_render_list(&hud_text, MEL_SCHEMA_TEXT);

mel_view_attach_source(main_view, mesh_source);
mel_view_attach_source(main_view, skinned_mesh_source);
mel_view_attach_source(main_view, transparent_mesh_source);
mel_view_attach_source(main_view, decal_source);
mel_view_attach_source(main_view, light_source);
mel_view_attach_source(main_view, debug_source);

mel_view_attach_source(photo_view, mesh_source);
mel_view_attach_source(photo_view, skinned_mesh_source);
mel_view_attach_source(photo_view, light_source);

mel_view_attach_source(minimap_view, mesh_source);
mel_view_attach_source(hud_view, hud_sprite_source);
mel_view_attach_source(hud_view, hud_text_source);

mel_frame_recipe_use_technique(frame_recipe, main_view, MEL_TECHNIQUE_SHADOW);
mel_frame_recipe_use_technique(frame_recipe, main_view, MEL_TECHNIQUE_MESH);
mel_frame_recipe_use_technique(frame_recipe, main_view, MEL_TECHNIQUE_DEBUG);
mel_frame_recipe_use_technique(frame_recipe, main_view, MEL_TECHNIQUE_POSTPROCESS);

mel_frame_recipe_use_technique(frame_recipe, photo_view, MEL_TECHNIQUE_SHADOW);
mel_frame_recipe_use_technique(frame_recipe, photo_view, MEL_TECHNIQUE_MESH);
mel_frame_recipe_use_technique(frame_recipe, photo_view, MEL_TECHNIQUE_POSTPROCESS);

mel_frame_recipe_use_technique(frame_recipe, minimap_view, MEL_TECHNIQUE_MESH);
mel_frame_recipe_use_technique(frame_recipe, hud_view,     MEL_TECHNIQUE_SPRITE);
mel_frame_recipe_use_technique(frame_recipe, hud_view,     MEL_TECHNIQUE_TEXT);
mel_frame_recipe_use_technique(frame_recipe, hud_view,     MEL_TECHNIQUE_IMGUI);

mel_frame_recipe_present(frame_recipe, main_view, game_swapchain);
mel_frame_recipe_overlay(frame_recipe, minimap_view, game_swapchain);
mel_frame_recipe_overlay(frame_recipe, hud_view, game_swapchain);

mel_frame_recipe_present(frame_recipe, photo_view, tools_swapchain);
mel_frame_recipe_overlay(frame_recipe, tools_debug_view, tools_swapchain);
```

### What The Engine Likely Compiles

```text
shadow atlas build
gbuffer pass for main_view
gbuffer pass for photo_view
lighting pass for main_view
lighting pass for photo_view
transparent pass for main_view
transparent pass for photo_view
debug overlay for main_view
postprocess for main_view
postprocess for photo_view
forward pass for minimap_view
hud sprite/text/imgui overlays
swapchain composition for game window
swapchain composition for tools window
```

### Why This Is Good

- the app says what views exist and what rendering families they want
- the engine figures out target reuse and pass expansion
- shadows and postprocess stay first-class engine features
- a tools window is not architecturally special

---

## Example 3: Ultra-Modern GPU-Driven Path

### What The Game Wants

- meshlets
- GPU visibility and culling
- task/mesh shaders where available
- visibility buffer or deferred path
- async compute setup
- dynamic resolution
- multiple captures: main gameplay, photo mode, reflection probes

### Sources

Some sources are still CPU-fed:

```c
static Mel_Source_Handle camera_source;
static Mel_Source_Handle debug_source;
static Mel_Source_Handle ui_text_source;
```

Some are mostly GPU-owned:

```c
static Mel_Source_Handle instance_source;
static Mel_Source_Handle meshlet_source;
static Mel_Source_Handle material_source;
static Mel_Source_Handle light_source;
static Mel_Source_Handle visible_instance_source;
static Mel_Source_Handle draw_packet_source;
```

The important point is that the app still talks about sources.
It does not manually build a pass DAG every frame.

### Views

```c
gameplay_view = mel_view_create(&(Mel_View_Desc){
    .name = S8("gameplay"),
    .camera = &player_camera,
    .render_scale = dynamic_resolution_scale,
});

photo_view = mel_view_create(&(Mel_View_Desc){
    .name = S8("photo"),
    .camera = &photo_camera,
});

probe_capture_view = mel_view_create(&(Mel_View_Desc){
    .name = S8("probe_capture"),
    .camera = &probe_camera,
    .target = reflection_probe_target,
});
```

### Recipe

```c
frame_recipe = mel_frame_recipe_create(S8("modern_game"));

mel_view_attach_source(gameplay_view, instance_source);
mel_view_attach_source(gameplay_view, meshlet_source);
mel_view_attach_source(gameplay_view, material_source);
mel_view_attach_source(gameplay_view, light_source);

mel_frame_recipe_use_technique(frame_recipe, gameplay_view, MEL_TECHNIQUE_SHADOW);
mel_frame_recipe_use_technique(frame_recipe, gameplay_view, MEL_TECHNIQUE_MESH);
mel_frame_recipe_use_technique(frame_recipe, gameplay_view, MEL_TECHNIQUE_POSTPROCESS);
mel_frame_recipe_use_technique(frame_recipe, gameplay_view, MEL_TECHNIQUE_DEBUG);

mel_frame_recipe_use_technique(frame_recipe, photo_view, MEL_TECHNIQUE_MESH);
mel_frame_recipe_use_technique(frame_recipe, photo_view, MEL_TECHNIQUE_POSTPROCESS);

mel_frame_recipe_use_technique(frame_recipe, probe_capture_view, MEL_TECHNIQUE_MESH);

mel_frame_recipe_present(frame_recipe, gameplay_view, game_swapchain);
mel_frame_recipe_present(frame_recipe, photo_view, tools_swapchain);
mel_frame_recipe_present(frame_recipe, probe_capture_view, capture_swapchain);
```

### Capability Resolution

The app requested `mesh`.

The engine may resolve that as:

```text
if mesh shaders supported:
    mesh -> mesh_shader.visibility_buffer
else if indirect + compute cull supported:
    mesh -> mesh.indirect.visibility_buffer
else:
    mesh -> mesh.forward
```

That decision belongs to the engine.
The app can inspect it, but should not need to rebuild its architecture around
it.

### What The Engine Likely Compiles

```text
async compute: instance compaction
async compute: meshlet culling
async compute: draw packet generation
graphics: shadow pass
graphics: visibility buffer fill via mesh/task shaders
graphics: material/light resolve
graphics: transparent or forward-only fallback path
graphics: postprocess chain
graphics: UI/debug overlays
present to gameplay swapchain
```

### Why This Is Good

- the high-level API is still the same architecture as the 2D case
- modern GPU paths are native engine techniques, not a separate universe
- fallback and capability selection are centralized
- the app can override pieces without hand-authoring the whole graph

---

## What Changes Between The Examples

The top-level abstraction does not change:

- sources
- views
- techniques
- frame recipe
- execution graph

What changes is:
- source types
- technique families
- number of views and swapchains
- how much graph expansion the engine performs underneath

That is the point.

Simple games should not learn low-level rendering topology.
Ambitious games should not outgrow the architecture.

---

## Escape Hatches

This stack still needs explicit escape hatches.

Examples:

```c
mel_frame_recipe_override_target(recipe, gameplay_view, hdr_debug_target);
mel_frame_recipe_inject_pass_after(recipe, gameplay_view, MEL_TECHNIQUE_SHADOW, my_pass);
mel_frame_recipe_disable_technique(recipe, gameplay_view, MEL_TECHNIQUE_POSTPROCESS);
mel_render_graph_add_pass(&graph, S8("research_pass"), ...);
```

The engine default should be strong.
It should never be a prison.

---

## What This Implies For Melody

If Melody follows this path, the engine should eventually provide first-class:

- render lists and other source types
- views
- swapchains
- frame recipes
- techniques
- graph compilation and execution
- inspection/debugging of resolved plans

The app should mainly say:

```text
this data exists
these views exist
these rendering families are wanted here
these swapchains should receive the result
```

Then the engine should make that true.
