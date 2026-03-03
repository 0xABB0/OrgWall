# Engine Usage Examples

Single-file applications demonstrating the engine from simple to complex.
Each example exercises more of the architecture.

The engine provides `main()`. The application implements `app_init` and
`app_shutdown`. The main loop, window driving, and frame boundaries are
handled automatically. All game logic flows through simulations — the
engine's central execution context. Simulations own update functions
and are registered with the engine to be driven each frame.

Status: these are vNext architecture examples. API shape follows split docs
(`engine.overview.md`, `engine.render.md`, `engine.assets.md`, etc.) and may
not match current implementation 1:1 yet.

---

## 1. Empty Window

Just a window. Clear to a color. Nothing else.

```c
#include "melody/window.h"

static Mel_Window_Handle window;

void app_init(void) {
    window = mel_window_create(S8("Hello"), 800, 600);
}

void app_shutdown(void) {
    mel_window_destroy(window);
}
```

Exercises: `mel_init`, `mel_shutdown`, `Mel_Window_Handle`. The engine drives
the window automatically — no update, no render, no loop code.

---

## 2. Static Sprite (Retained)

Load a texture, push one sprite into a retained list. It stays forever.

```c
#include "melody/vfs.h"
#include "melody/window.h"
#include "melody/texture.h"
#include "melody/render.graph.h"
#include "melody/sprite.entry.h"

static Mel_Window_Handle window;

void app_init(void) {
    mel_vfs_mount(S8("assets"), mel_vfs_backend_os());
    window = mel_window_create(S8("Sprite"), 800, 600);
    Mel_Texture_Handle hero = mel_load_texture(S8("hero.png"));

    Mel_Render_Graph* graph = mel_window_graph(window);
    Mel_Render_List* sprites = mel_render_graph_get_list(graph, S8("sprites"));

    Mel_Sprite_Entry* e = mel_render_list_push(sprites, 0);
    *e = (Mel_Sprite_Entry){
        .pos  = { 400, 300 },
        .size = { 64, 64 },
        .tex  = hero,
        .color = 0xFFFFFFFF,
    };
}

void app_shutdown(void) {
    mel_window_destroy(window);
}
```

Exercises: async texture loading (handle valid immediately, fallback while
loading), retained render list — push once at init, entry persists, rendered
every frame without CPU work. No updates, no hooks, no per-frame code.

---

## 3. Moving Sprite with Interpolation

Fixed-timestep simulation + variable-rate rendering. The sprite bounces.

```c
#include "melody/vfs.h"
#include "melody/window.h"
#include "melody/texture.h"
#include "melody/render.graph.h"
#include "melody/sprite.entry.h"
#include "melody/sim.ctx.h"
#include "melody/math.vec2.h"

static Mel_Window_Handle window;
static Mel_Sim_Ctx sim;
static Mel_Sim_Fixed* physics;
static u8 event_buf[4096];
static Mel_Texture_Handle ball;
static Mel_Vec2 pos, prev_pos, vel;

void physics_tick(Mel_Sim_Ctx* sim, f32 dt, void* user) {
    prev_pos = pos;
    pos.x += vel.x * dt;
    pos.y += vel.y * dt;
    if (pos.x < 0 || pos.x > 800) vel.x = -vel.x;
    if (pos.y < 0 || pos.y > 600) vel.y = -vel.y;
}

void push_sprites(Mel_Sim_Ctx* sim, f32 dt, void* user) {
    Mel_Render_Graph* graph = mel_window_graph(window);
    Mel_Render_List* sprites = mel_render_graph_get_list(graph, S8("sprites"));
    f32 alpha = mel_sim_fixed_alpha(physics);
    Mel_Vec2 draw_pos = mel_vec2_lerp(prev_pos, pos, alpha);

    Mel_Sprite_Entry* e = mel_render_list_push(sprites, 0);
    *e = (Mel_Sprite_Entry){
        .pos  = draw_pos,
        .size = { 32, 32 },
        .tex  = ball,
        .color = 0xFFFFFFFF,
    };
}

void app_init(void) {
    mel_vfs_mount(S8("assets"), mel_vfs_backend_os());
    window = mel_window_create(S8("Bounce"), 800, 600);
    ball = mel_load_texture(S8("ball.png"));
    pos = (Mel_Vec2){ 400, 300 };
    vel = (Mel_Vec2){ 200, 150 };
    prev_pos = pos;

    mel_sim_init(&sim, .event_buffer = event_buf, .event_buffer_size = sizeof(event_buf));
    physics = mel_sim_add_fixed(&sim, .fixed_dt = 1.0f / 60.0f);
    mel_sim_fixed_add_update(physics, physics_tick);
    mel_sim_add_variable(&sim, push_sprites);
    mel_sim_register(&sim);
}

void app_shutdown(void) {
    mel_sim_unregister(&sim);
    mel_window_destroy(window);
}
```

Exercises: simulation with a fixed context (`physics` at 60Hz) and a
variable update (`push_sprites`). The fixed context owns the accumulator
and alpha. `mel_sim_fixed_alpha(physics)` gives the interpolation factor.
Fixed ticks run first (may fire multiple times per frame), then variable
updates run once, then windows render.

---

## 4. ECS Sprites (Auto Sync)

Use ECS with the default sync systems. No manual render list code.
Once world->graph routing is configured, add components and sprites appear.

```c
#include "melody/vfs.h"
#include "melody/window.h"
#include "melody/texture.h"
#include "melody/ecs.world.h"
#include "melody/ecs.2d.sprite.h"
#include "melody/ecs.2d.transform.h"

static Mel_Window_Handle window;
static Mel_World_Handle world;

void app_init(void) {
    mel_vfs_mount(S8("assets"), mel_vfs_backend_os());
    window = mel_window_create(S8("ECS"), 800, 600);
    world = mel_world_create();

    Mel_Texture_Handle tex = mel_load_texture(S8("enemy.png"));
    ecs_world_t* ecs = mel_world_ecs(world);

    for (i32 i = 0; i < 100; i++) {
        ecs_entity_t e = ecs_new(ecs);
        ecs_set(ecs, e, Mel_CTransform, {
            .pos = { (f32)(i % 10) * 64, (f32)(i / 10) * 64 },
        });
        ecs_set(ecs, e, Mel_Sprite, {
            .size = { 48, 48 },
            .color = 0xFFFFFFFF,
            .texture = tex,
        });
    }
}

void app_shutdown(void) {
    mel_world_destroy(world);
    mel_window_destroy(window);
}
```

Exercises: `Mel_World_Handle`, ECS world independent from everything else,
default sync systems (sprite+transform → sprites render list automatically
once routed), zero manual render list code, zero update registrations.

Note: the sync systems need to know which render graph's lists to write to.
That wiring happens during setup (open question #4 in engine.render.md).

---

## 5. Multi-Camera (Minimap)

One window, two cameras. Main view + top-down minimap in the corner.
Same sprite list drawn twice from different viewpoints.

```c
#include "melody/vfs.h"
#include "melody/window.h"
#include "melody/texture.h"
#include "melody/ecs.world.h"
#include "melody/render.graph.h"
#include "melody/ecs.2d.sprite.h"
#include "melody/ecs.2d.transform.h"

static Mel_Window_Handle window;
static Mel_World_Handle world;
static Mel_Camera main_cam;
static Mel_Camera minimap_cam;

void app_init(void) {
    mel_vfs_mount(S8("assets"), mel_vfs_backend_os());
    window = mel_window_create(S8("Minimap"), 1280, 720);
    world = mel_world_create();

    Mel_Texture_Handle tex = mel_load_texture(S8("tree.png"));
    ecs_world_t* ecs = mel_world_ecs(world);

    for (i32 i = 0; i < 500; i++) {
        ecs_entity_t e = ecs_new(ecs);
        ecs_set(ecs, e, Mel_CTransform, {
            .pos = { mel_rng_f32() * 2000, mel_rng_f32() * 2000 },
        });
        ecs_set(ecs, e, Mel_Sprite, {
            .size = { 32, 32 },
            .color = 0xFF00FF00,
            .texture = tex,
        });
    }

    main_cam = mel_camera_ortho(0, 0, 1280, 720);
    minimap_cam = mel_camera_ortho(0, 0, 2000, 2000);

    Mel_Render_Graph* graph = mel_window_graph(window);
    mel_render_graph_add_pass(graph, S8("minimap"), &(Mel_Pass_Desc){
        .lists = { S8("sprites") },
        .camera = &minimap_cam,
        .viewport = { 1280 - 256, 0, 256, 256 },
        .fn = mel_pass_sprites,
    });
}

void app_shutdown(void) {
    mel_world_destroy(world);
    mel_window_destroy(window);
}
```

Exercises: same sprite list consumed by two passes (default opaque pass with
main camera + minimap pass with top-down camera). Camera-agnostic lists in
action. Custom pass added to the default graph at init.

---

## 6. Multi-Window (Game + Editor)

Two windows. Both observe the same ECS world.

```c
#include "melody/vfs.h"
#include "melody/window.h"
#include "melody/texture.h"
#include "melody/ecs.world.h"
#include "melody/ecs.2d.sprite.h"
#include "melody/ecs.2d.transform.h"

static Mel_Window_Handle game_win;
static Mel_Window_Handle editor_win;
static Mel_World_Handle world;

void app_init(void) {
    mel_vfs_mount(S8("assets"), mel_vfs_backend_os());
    game_win   = mel_window_create(S8("Game"),   1280, 720);
    editor_win = mel_window_create(S8("Editor"), 1920, 1080);
    world = mel_world_create();

    Mel_Texture_Handle tex = mel_load_texture(S8("player.png"));
    ecs_world_t* ecs = mel_world_ecs(world);

    ecs_entity_t player = ecs_new(ecs);
    ecs_set(ecs, player, Mel_CTransform, { .pos = { 640, 360 } });
    ecs_set(ecs, player, Mel_Sprite, {
        .size = { 64, 64 },
        .color = 0xFFFFFFFF,
        .texture = tex,
    });
}

void app_shutdown(void) {
    mel_world_destroy(world);
    mel_window_destroy(editor_win);
    mel_window_destroy(game_win);
}
```

Exercises: two windows sharing the same world and asset pools. With routing
configured per window, both render graphs consume the same ECS world while the
engine drives both windows automatically.
Note: for independent refresh rates with per-window cadence, the manual path
gives finer control — see engine.app.md "Manual Path."

---

## 7. Custom Pipeline (Particles)

Custom render list type + custom pass + production hook. Engine doesn't know
about particles. You define the data, register the list, write the pass,
register a producer.

```c
#include "melody/window.h"
#include "melody/render.graph.h"
#include "melody/sim.ctx.h"
#include "melody/gpu.cmd.h"

typedef struct {
    Mel_Vec2 pos;
    Mel_Vec2 vel;
    f32 size;
    f32 lifetime;
    u32 color;
} Particle_Entry;

static Mel_Window_Handle window;
static Mel_Sim_Ctx sim;
static u8 event_buf[1024];
static VkPipeline particle_pipeline;
static Particle_Emitter emitter;

void particle_pass(Mel_Render_Pass_Ctx* ctx) {
    Mel_Render_List* list = ctx->lists[0];
    vkCmdBindPipeline(ctx->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, particle_pipeline);
}

void produce_particles(Mel_Render_List* list, void* user) {
    Particle_Emitter* em = user;
    for (u32 i = 0; i < em->count; i++) {
        Particle_Entry* e = mel_render_list_push(list,
            mel_sort_key_transparent(em->particles[i].depth));
        *e = (Particle_Entry){
            .pos = em->particles[i].pos,
            .vel = em->particles[i].vel,
            .size = em->particles[i].size,
            .lifetime = em->particles[i].lifetime,
            .color = em->particles[i].color,
        };
    }
}

void sim_tick(Mel_Sim_Ctx* sim, f32 dt, void* user) {
    Particle_Emitter* em = user;
    particle_emitter_tick(em, dt);
}

void app_init(void) {
    window = mel_window_create(S8("Particles"), 800, 600);

    Mel_Render_Graph* graph = mel_window_graph(window);
    mel_render_graph_register_list(graph, S8("particles"), sizeof(Particle_Entry));

    Mel_Render_List* particles = mel_render_graph_get_list(graph, S8("particles"));
    mel_render_list_add_producer(particles, produce_particles, &emitter);

    mel_render_graph_add_pass_after(graph, S8("transparent"), S8("particles"),
        &(Mel_Pass_Desc){
            .lists = { S8("particles") },
            .camera = &main_camera,
            .viewport = full_viewport,
            .fn = particle_pass,
        });

    mel_sim_init(&sim, .event_buffer = event_buf, .event_buffer_size = sizeof(event_buf));
    Mel_Sim_Fixed* tick = mel_sim_add_fixed(&sim, .fixed_dt = 1.0f / 60.0f);
    mel_sim_fixed_add_update(tick, sim_tick, .user = &emitter);
    mel_sim_register(&sim);
}

void app_shutdown(void) {
    mel_sim_unregister(&sim);
    mel_window_destroy(window);
}
```

Exercises: custom entry type, custom render list, production hook, custom pass
injected into the default graph. Simulation with a fixed context ticks the
emitter. Producer populates the list. Pass renders it. All wired at init,
runs automatically.

---

## 8. Raw Vulkan (Single Pass)

No render lists, no default graph. Register a single raw pass.
Maximum control, minimum engine.

```c
#include "melody/window.h"
#include "melody/render.graph.h"

static Mel_Window_Handle window;
static VkPipeline my_pipeline;
static VkBuffer my_vertex_buffer;

void my_pass(Mel_Render_Pass_Ctx* ctx) {
    vkCmdBindPipeline(ctx->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, my_pipeline);
    vkCmdBindVertexBuffers(ctx->cmd, 0, 1, &my_vertex_buffer, &(VkDeviceSize){0});
    vkCmdDraw(ctx->cmd, 3, 1, 0, 0);
}

void app_init(void) {
    window = mel_window_create(S8("Raw"), 800, 600);

    Mel_Render_Graph* graph = mel_window_graph(window);
    mel_render_graph_clear(graph);
    mel_render_graph_add_pass(graph, S8("my_pass"), &(Mel_Pass_Desc){
        .fn = my_pass,
    });
}

void app_shutdown(void) {
    mel_window_destroy(window);
}
```

Exercises: `mel_render_graph_clear` removes the default passes. You register
your own pass that gets a command buffer via the pass context. The engine still
manages swapchain, frame sync, and graph execution. You own what happens
inside the pass.

---

## 9. Headless CLI Tool

No GPU, no window. Just the engine's non-rendering subsystems.

```c
#include "melody/vfs.h"

void app_init(void) {
    mel_vfs_mount(S8("data"), mel_vfs_backend_os());

    Mel_Vfs* vfs = mel_vfs_instance();
    Mel_Vfs_Stat stat = {0};
    if (mel_vfs_stat_sync(vfs, S8("data/manifest.json"), &stat)) {
        printf("Processed %llu bytes\n", (unsigned long long)stat.size);
    }
    mel_quit();
}

void app_shutdown(void) {
}
```

Exercises: headless entry point — no GPU init, no window, no swapchain. VFS,
IO, job system, allocators all still available. `mel_quit()` signals exit.

---

## 10. Loading Screen (Asset Gates)

Load a batch of assets, show progress, transition to gameplay when ready.

```c
#include "melody/vfs.h"
#include "melody/window.h"
#include "melody/texture.h"
#include "melody/tile.map.h"
#include "melody/render.graph.h"
#include "melody/sim.ctx.h"

static Mel_Window_Handle window;
static Mel_Sim_Ctx sim;
static u8 event_buf[4096];
static Mel_Texture_Handle hero_tex, enemy_tex;
static Mel_Tilemap_Handle level_map;
static Mel_Load_Gate gate;
static Mel_Update_Handle loading_h;

void loading_tick(Mel_Sim_Ctx* sim, f32 dt, void* user) {
    f32 progress = mel_load_gate_progress(&gate);
    update_loading_bar(progress);

    if (mel_load_gate_is_loaded(&gate)) {
        spawn_level(level_map);
        mel_sim_remove_variable(sim, loading_h);
        Mel_Sim_Fixed* physics = mel_sim_add_fixed(sim, .fixed_dt = 1.0f / 60.0f);
        mel_sim_fixed_add_update(physics, gameplay_tick);
    }
}

void app_init(void) {
    mel_vfs_mount(S8("assets"), mel_vfs_backend_os());
    window = mel_window_create(S8("Game"), 1280, 720);

    hero_tex  = mel_load_texture(S8("hero.png"));
    enemy_tex = mel_load_texture(S8("enemy.png"));
    level_map = mel_load_tilemap(S8("level1.tmx"));

    gate = mel_load_gate_create();
    mel_load_gate_add_texture(&gate, hero_tex);
    mel_load_gate_add_texture(&gate, enemy_tex);
    mel_load_gate_add_tilemap(&gate, level_map);

    mel_sim_init(&sim, .event_buffer = event_buf, .event_buffer_size = sizeof(event_buf));
    loading_h = mel_sim_add_variable(&sim, loading_tick);
    mel_sim_register(&sim);
}

void app_shutdown(void) {
    mel_sim_unregister(&sim);
    mel_window_destroy(window);
}
```

Exercises: `Mel_Load_Gate` — batch query across multiple asset types.
`mel_load_gate_progress` for loading bars. Loading phase uses a variable
update (no fixed timestep needed — just polling). On completion, removes
the variable update and adds a fixed context with gameplay. Same sim
transitions from loading to gameplay via deferred mutations.

---

## 11. Deterministic Simulation (Replay / Attract Mode)

Two simulation contexts driving two worlds from the same seed. One takes
live input, the other replays recorded input. Both produce identical results.

```c
#include "melody/vfs.h"
#include "melody/window.h"
#include "melody/texture.h"
#include "melody/ecs.world.h"
#include "melody/ecs.2d.sprite.h"
#include "melody/ecs.2d.transform.h"
#include "melody/sim.ctx.h"
#include "melody/render.graph.h"

#define EVT_MOVE 1

typedef struct {
    f32 dx, dy;
} Move_Event;

typedef struct {
    Move_Event recorded[1024];
    u32 count;
} Recording;

static Mel_Window_Handle window;

static Mel_Sim_Ctx live_sim;
static Mel_World_Handle live_world;
static u8 live_events[4096];

static Mel_Sim_Ctx replay_sim;
static Mel_World_Handle replay_world;
static u8 replay_events[4096];

static Recording recording;
static bool replaying;

void spawn_scene(Mel_World_Handle world, Mel_Texture_Handle tex) {
    ecs_world_t* ecs = mel_world_ecs(world);
    for (i32 i = 0; i < 20; i++) {
        ecs_entity_t e = ecs_new(ecs);
        ecs_set(ecs, e, Mel_CTransform, { .pos = { 100 + i * 50.0f, 300 } });
        ecs_set(ecs, e, Mel_Sprite, {
            .size = { 32, 32 },
            .color = 0xFFFFFFFF,
            .texture = tex,
        });
    }
}

typedef struct {
    Mel_World_Handle world;
    Mel_Sim_Fixed* fixed;
    Recording* recording;
} Sim_Data;

void tick_world(Mel_Sim_Ctx* sim, Mel_World_Handle world, f32 dt) {
    ecs_world_t* ecs = mel_world_ecs(world);

    Mel_Sim_Iter iter = {0};
    Move_Event* move;
    while ((move = mel_sim_next(sim, EVT_MOVE, &iter)) != NULL) {
        f32 jitter = mel_rng_f32_range(&sim->rng, -5.0f, 5.0f);
        // ... move player by (move->dx + jitter, move->dy) ...
    }

    ecs_progress(ecs, dt);
}

void live_tick(Mel_Sim_Ctx* sim, f32 dt, void* user) {
    Sim_Data* data = user;
    Move_Event move = get_input_movement();
    mel_sim_push(sim, EVT_MOVE, &move, sizeof(move));

    if (data->recording->count < 1024)
        data->recording->recorded[data->recording->count++] = move;

    tick_world(sim, data->world, dt);
}

void replay_tick(Mel_Sim_Ctx* sim, f32 dt, void* user) {
    Sim_Data* data = user;
    if (data->fixed->tick < data->recording->count) {
        Move_Event* move = &data->recording->recorded[data->fixed->tick];
        mel_sim_push(sim, EVT_MOVE, move, sizeof(*move));
    }
    tick_world(sim, data->world, dt);
}

static Sim_Data live_data;
static Sim_Data replay_data;

void app_init(void) {
    mel_vfs_mount(S8("assets"), mel_vfs_backend_os());
    window = mel_window_create(S8("Replay"), 1280, 720);
    Mel_Texture_Handle tex = mel_load_texture(S8("unit.png"));

    u64 seed = 12345;

    live_world = mel_world_create();
    spawn_scene(live_world, tex);
    mel_sim_init(&live_sim,
        .seed = seed,
        .event_buffer = live_events,
        .event_buffer_size = sizeof(live_events));
    Mel_Sim_Fixed* live_phys = mel_sim_add_fixed(&live_sim, .fixed_dt = 1.0f / 60.0f);
    live_data = (Sim_Data){ .world = live_world, .fixed = live_phys, .recording = &recording };
    mel_sim_fixed_add_update(live_phys, live_tick, .user = &live_data);
    mel_sim_register(&live_sim);

    replay_world = mel_world_create();
    spawn_scene(replay_world, tex);
    mel_sim_init(&replay_sim,
        .seed = seed,
        .event_buffer = replay_events,
        .event_buffer_size = sizeof(replay_events));
    Mel_Sim_Fixed* replay_phys = mel_sim_add_fixed(&replay_sim, .fixed_dt = 1.0f / 60.0f);
    replay_data = (Sim_Data){ .world = replay_world, .fixed = replay_phys, .recording = &recording };
    mel_sim_fixed_add_update(replay_phys, replay_tick, .user = &replay_data);
    // not registered yet — register when switching to replay
}

void app_shutdown(void) {
    if (replay_sim.registered)
        mel_sim_unregister(&replay_sim);
    mel_sim_unregister(&live_sim);
    mel_world_destroy(replay_world);
    mel_world_destroy(live_world);
    mel_window_destroy(window);
}
```

Exercises: two separate simulations, each with their own update, each
driving their own world. Same seed + same events = same deterministic
outcome. The live sim is registered at startup; the replay sim is set up
but not registered — register it (and unregister live) to switch to replay
mode. Each sim's update callback receives the sim, so it can push events
and access the deterministic RNG directly.

This is how title screen attract modes work (Doom), how replay systems work,
and how automated testing works (replay a known input sequence, assert the
world state after N ticks).

---

## Progression

1. **Empty Window** — `app_init`, window, engine drives it
2. **Static Sprite** — texture loading, retained list push
3. **Moving Sprite** — simulation, fixed + variable updates, interpolation
4. **ECS Sprites** — world, auto sync, zero manual code
5. **Multi-Camera** — same list, two passes, two viewpoints
6. **Multi-Window** — two windows, shared world, both auto-driven
7. **Custom Pipeline** — custom list type, producer hook, custom pass, sim
8. **Raw Vulkan** — clear default graph, single raw pass
9. **Headless CLI** — no GPU at all
10. **Loading Screen** — asset gates, progress, dynamic update swapping within a sim
11. **Deterministic Replay** — two sims, registration switching, seeded RNG, event playback

## Three Ways to Populate Render Lists

**Retained pushes** — push once, entry stays until explicitly removed. Best
for static or rarely-changing content. Pushed during init or in response to
game events.

```c
u32 idx = sprite_node.entry_index;            // index captured by your owner/system
Mel_Sprite_Entry* e = mel_render_list_get(list, idx); // update in place
e->color = 0xFF00FFFF;
mel_render_list_remove(list, idx);            // when done
```

**Production hooks** — registered at init (or anytime). Called automatically
each frame before graph execution. Best for dynamic content that needs
rebuilding every frame from external state.

```c
mel_render_list_add_producer(list, my_producer_fn, user_data);
mel_render_list_remove_producer(list, my_producer_fn);
```

**Ephemeral pushes in variable updates** — grab a list, push entries. Frame
arena clears them next frame. Best for one-off dynamic content computed
per-frame (interpolated positions, debug draws, etc.).

```c
void frame_tick(Mel_Sim_Ctx* sim, f32 dt, void* user) {
    Mel_Render_List* sprites = mel_render_graph_get_list(mel_window_graph(window), S8("sprites"));
    Mel_Sprite_Entry* e = mel_render_list_push(sprites, sort_key);
    *e = (Mel_Sprite_Entry){ ... };
}
```

All three paths feed the same render lists. All are consumed by the same graph.
