# Task: Decouple Window from Engine

Parent goal: move from current `Mel_Engine` god struct to the vNext architecture
described in `engine.overview.md` and `engine.app.md`.

Trigger: headless tests are blocked — can't init the engine without a window.

## Context

Current state:
- `Mel_Engine` holds `SDL_Window*`, `Mel_Swapchain`, `Mel_Render_Graph*`,
  `Mel_Sprite_Pass*`, `Mel_Texture_Pool*`, `VkDescriptorPool` (imgui), plus
  frame timing and sim list
- `mel_gpu_device_init` requires `SDL_Window*` — creates VkSurfaceKHR from it
- `mel_engine_frame` queries window flags, drives imgui, executes render graph
- `mel_engine_process_event` matches events by window ID
- `Mel_App` embeds `Mel_Engine` by value
- Game creates SDL_Window in `app_init`, passes to `mel_engine_init`

Target state (from design docs):
- No `Mel_Engine` struct — modules own their own state (module statics)
- `mel_init()` / `mel_shutdown()` bring up all subsystems
- GPU device works without a window (headless GPU for visual tests, compute)
- Window registry with handles (`Mel_Window_Handle`), each window owns its
  swapchain + render target
- App entry via link-based functions (`app_init`, `app_shutdown`, optional `app_event`)
- Engine auto-cleanup — app doesn't implement shutdown for engine-owned resources

## Steps

Each step leaves the codebase compilable and all existing tests/game working.

### Step 1: Split GPU device init from surface creation
- [ ] `mel_gpu_device_init` — instance, physical device, logical device, VMA.
      No window param. Always include SDL Vulkan instance extensions (needed for
      MoltenVK on macOS even without presentation).
- [ ] Device rating: if no surface exists, skip present queue check and swapchain
      extension requirement. Pick graphics-capable queue for present_family as fallback.
- [ ] `mel_gpu_surface_create(dev, window) -> VkSurfaceKHR` — creates surface,
      verifies present support on the chosen physical device.
- [ ] `Mel_Gpu_Device.surface` becomes nullable (VK_NULL_HANDLE when headless).
- [ ] Update `mel_engine_init` to call both: device init, then surface create + store.
- [ ] Existing behavior unchanged — game still passes window, still gets a surface.

### Step 2: Window registry with handles
- [ ] New files: `window.h`, `window.c`
- [ ] `Mel_Window_Handle` — generational handle (slotmap-style)
- [ ] `Mel_Window` struct per design doc: SDL_Window*, surface, swapchain,
      render_target, input_state, frame timing, resize flag
- [ ] `mel_window_create(title, w, h, ...) -> Mel_Window_Handle`
- [ ] `mel_window_destroy(handle)`
- [ ] `mel_window_get(handle) -> Mel_Window*` (assert on stale handle)
- [ ] `mel_window_target(handle) -> Mel_Render_Target*` (for render graph)
- [ ] `mel_window_sdl(handle) -> SDL_Window*` (escape hatch)
- [ ] Registry is module-static, initialized by mel_init (or manually).
- [ ] Swapchain init/resize/shutdown managed per-window inside the registry.
- [ ] Window destroy cleans up swapchain + surface + SDL_Window.

### Step 3: Gut Mel_Engine
- [ ] Remove from `Mel_Engine`: window, swapchain, render_graph, sprite_pass,
      texture_pool, imgui_pool, imgui_initialized, resize_requested
- [ ] What stays (temporarily): dev, allocator, tracking, sim_head, frame timing, features
- [ ] `mel_engine_init` becomes: allocator setup + GPU device init (no window)
- [ ] Sprite pass, texture pool become module statics (initialized in mel_init or lazily)
- [ ] ImGui becomes per-window (or deferred to a later step — mark as TODO)
- [ ] Update game/main.c: create window via registry, wire render graph to
      `mel_window_target(handle)` instead of `&s_swapchain_target`
- [ ] Update all examples/demos that use `Mel_Engine` fields

### Step 4: Split engine frame
- [ ] `mel_engine_tick(f32 dt)` — tick all registered simulations. No GPU, no window.
- [ ] Window rendering driven separately — either by the app explicitly
      (manual path) or by `mel__drive_windows()` which iterates all windows
      in the registry and does: check visibility, acquire swapchain, execute
      render graph passes that target this window, present.
- [ ] `mel_engine_frame` becomes a thin wrapper: tick + drive_windows.
      Deprecated in favor of the split calls.
- [ ] Headless path: just call `mel_engine_tick` in a loop. No windows, no rendering.

### Step 5: Rework Mel_App / entry point
- [ ] Move toward `engine.app.md` vNext: engine provides `main()`, app implements
      `app_init()` / `app_shutdown()` / optional `app_event()`
- [ ] `Mel_App` struct shrinks or disappears — state moves to module statics
- [ ] `mel_init()` / `mel_shutdown()` replace `mel_engine_init()` / `mel_engine_shutdown()`
- [ ] Engine auto-cleanup: `mel_shutdown()` destroys all windows, GPU device, pools,
      tracking allocator — app only cleans up its own stuff
- [ ] `on_shutdown` becomes optional (app resources only, not engine resources)
- [ ] Test harness can call `mel_init()` without any window for headless tests,
      or `mel_init()` + `mel_window_create()` for visual tests

### Step 6: Update game/main.c to new model
- [ ] Remove `app_shutdown` engine cleanup (engine auto-cleans)
- [ ] Window via `mel_window_create`, not raw `SDL_CreateWindow`
- [ ] Render graph wired to `mel_window_target(handle)`
- [ ] Editors, VFS, IO, game state — app-owned, cleaned up by app

### Step 7: Update all examples/demos
- [ ] Each example uses `mel_init()` + `mel_window_create()` instead of
      `mel_engine_init()`
- [ ] Examples that are purely algorithmic (trie, rbtree, etc.) could potentially
      work headless if they don't need rendering

### Deferred (not part of this task)
- Hot-reload DLL event model (single `app_event` callback with lifecycle events)
- Multi-window rendering with different refresh rates
- Per-window ImGui contexts
- Headless compute (GPU without any window — needs VK_EXT_headless_surface or similar)

## Open Questions (answered during discussion)
- GPU device as value or pointer in engine struct?
  -> Pointer. Engine struct is going away eventually, pointer makes transition cleaner.
- Always include SDL Vulkan extensions even for headless?
  -> Yes. Required for MoltenVK on macOS regardless.
- ImGui per-window now or later?
  -> Later. Mark as TODO. For now, ImGui stays single-window.
- Hot-reload event model now or later?
  -> Later. Just clean up current callback model.

## Related todo.md items
- [ENGINE] make melody run "headless"
- [ENGINE] migrate legacy Mel_Engine + SDL_Window coupling
- [ENGINE] unify window creation
- [CORE] Hot-Reloading for Game Code
- [RENDER] support multiple windows/viewports/cameras
- [CORE] melody should handle much more initialization autonomously
