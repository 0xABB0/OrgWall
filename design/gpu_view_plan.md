# Plan: Mel_NGpuView + GPU Device Decoupling

## Problem

The engine is supposed to remove boilerplate, not dictate structure. Currently:

1. `game/main.c` is ~700 lines, ~500 of which are ceremony (Vulkan init, swapchain, frame timing, image barriers, ImGui lifecycle, resize handling)
2. `Mel_Gpu_Device` is coupled to a single `SDL_Window*` — it creates the Vulkan surface during device init, making multi-window/multi-viewport impossible
3. GPU rendering bypasses the existing `Mel_NCtrl` native UI system entirely — the game creates an `SDL_Window` directly, ignoring `Mel_NWindow`
4. SDL leaks into the game layer (the game should never see SDL)

## Goal

A GPU-rendered view (`Mel_NGpuView`) that plugs into the existing native control hierarchy as a regular child. Each view owns its own Vulkan surface + swapchain. The GPU device is shared and decoupled from any window.

```c
static void app_init(Mel_App* app) {
    mel_gpu_device_init(&s_dev, .enable_validation = true, .app_name = S8("My Game"));

    mel_nwindow_init(&s_window, .title = S8("My Game"), .width = 640, .height = 480);
    mel_ngpuview_init(&s_view, .device = &s_dev, .on_render = my_render);
    mel_nctrl_add_child(&s_window.base, &s_view.base);
    mel_nwindow_show(&s_window);
}
```

Multi-viewport editor:
```c
mel_nwindow_init(&s_window, ...);
mel_ngpuview_init(&s_scene_view, .device = &s_dev, .on_render = render_scene);
mel_ngpuview_init(&s_preview_view, .device = &s_dev, .on_render = render_preview);
mel_nbutton_init(&s_button, .text = S8("Play"));
mel_nctrl_add_child(&s_window.base, &s_scene_view.base);
mel_nctrl_add_child(&s_window.base, &s_preview_view.base);
mel_nctrl_add_child(&s_window.base, &s_button.base);
```

No engine struct wrapping everything. No framework running your loop. Just building blocks.

## Design Philosophy

The engine removes boilerplate by providing well-designed building blocks, not by hiding complexity behind a god-object. The app owns the flow. Melody provides the pieces.

## Architectural Changes

### 1. Decouple `Mel_Gpu_Device` from windows/surfaces

**Current:** `mel_gpu_device_init` takes `SDL_Window*`, creates `VkSurfaceKHR`, uses surface for physical device selection (present queue support check).

**New:** `mel_gpu_device_init` creates instance + selects physical device + creates logical device WITHOUT any surface. The surface is no longer part of the device.

The tricky part: present queue family selection currently needs a surface. Options:
- (a) Assume graphics queue supports present (true on every real GPU — macOS/MoltenVK, all desktop GPUs, all mobile GPUs). Pick graphics family as present family. Assert at surface creation time.
- (b) Defer present queue selection to first gpu view creation.
- (c) Let the app provide a temporary surface for device selection, then destroy it.

Recommendation: **(a)**. It's true everywhere Melody targets. If we ever hit a GPU where it isn't, we deal with it then (MEL-COMMAND-VII — don't serve phantoms).

**Changes to `Mel_Gpu_Device`:**
- Remove `SDL_Window*` from `Mel_Gpu_Device_Opt`
- Remove `VkSurfaceKHR surface` from `Mel_Gpu_Device` struct
- Surface creation moves to `Mel_NGpuView`
- Vulkan library loading (currently `SDL_Vulkan_LoadLibrary`) needs to happen separately — either in `mel__engine_init()` or as part of a platform init step
- Instance extension enumeration: currently uses `SDL_Vulkan_GetInstanceExtensions` to get surface extensions. Without SDL, we need to specify them manually per platform (`VK_KHR_surface` + `VK_EXT_metal_surface` on macOS, `VK_KHR_win32_surface` on Windows, etc.)
- `SDL_Vulkan_GetVkGetInstanceProcAddr` → we can use volk's default loader, or load the Vulkan library ourselves

**Note:** This means `gpu.device` no longer depends on SDL at all. The SDL includes in `gpu.device.h` go away.

### 2. New: `Mel_NGpuView` — a native control with GPU rendering

Files: `ui.native.gpuview.h`, `ui.native.gpuview.c`, `melody/osx/ui.native.gpuview.osx.m`

`Mel_NGpuView` is a `Mel_NCtrl` subtype. On the platform side, it creates a native view with a `CAMetalLayer` (macOS). From that, it creates a `VkSurfaceKHR` directly (no SDL involvement). It owns its swapchain, render frame, and handles resize.

```c
typedef void (*Mel_NGpuView_Render_Cb)(Mel_NGpuView* view, Mel_Gpu_Cmd* cmd, void* user);

typedef struct {
    Mel_NCtrl base;
    Mel_Gpu_Device* dev;
    VkSurfaceKHR surface;
    Mel_Gpu_Swapchain swapchain;
    Mel_Render_Frame frame;
    Mel_NGpuView_Render_Cb on_render;
    void* user_data;
    bool resize_requested;
} Mel_NGpuView;

typedef struct {
    Mel_Gpu_Device* device;
    Mel_NGpuView_Render_Cb on_render;
    void* user_data;
} Mel_NGpuView_Opt;

void mel_ngpuview_init_opt(Mel_NGpuView* view, Mel_NGpuView_Opt opt);
#define mel_ngpuview_init(view, ...) mel_ngpuview_init_opt((view), (Mel_NGpuView_Opt){__VA_ARGS__})
```

**Platform backing (macOS):**
- `create_backing`: Creates an `NSView` with `wantsLayer = YES` and a `CAMetalLayer` as its layer
- From the `CAMetalLayer`, creates a `VkSurfaceKHR` via `vkCreateMetalSurfaceEXT`
- Then inits the swapchain and render frame
- The `set_frame` vtable entry triggers resize (swapchain recreation)

**Render loop:**
The gpu view doesn't run its own loop. It provides a function the app calls when it wants a frame rendered:

```c
void mel_ngpuview_render(Mel_NGpuView* view);
```

This function:
1. Handles deferred resize if needed
2. Calls `mel_render_frame_begin` (acquire swapchain image)
3. Builds `Mel_Gpu_Cmd`
4. Calls `view->on_render(view, &cmd, view->user_data)`
5. Calls `mel_render_frame_end` (submit + present)

The app decides WHEN to call this. From an SDL iterate callback, from a `CVDisplayLink`, from a manual loop, whatever.

**Convenience: begin/end swapchain pass:**

```c
typedef struct { f32 clear_r, clear_g, clear_b, clear_a; } Mel_NGpuView_Pass_Opt;

void mel_ngpuview_begin_pass_opt(Mel_NGpuView* view, Mel_Gpu_Cmd* cmd, Mel_NGpuView_Pass_Opt opt);
#define mel_ngpuview_begin_pass(view, cmd, ...) ...
void mel_ngpuview_end_pass(Mel_NGpuView* view, Mel_Gpu_Cmd* cmd);
```

These do the image barriers, viewport/scissor setup, color attachment, begin/end rendering. The stuff that's pure ceremony and identical for every swapchain pass. The app can skip these and do it manually if it needs custom behavior.

### 3. Swapchain decoupling

Currently `Mel_Gpu_Swapchain` uses `dev->surface` directly. Since the surface now lives on the gpu view, swapchain init needs to take the surface as a parameter.

```c
typedef struct {
    VkSurfaceKHR surface;    // NEW — was implicit from dev->surface
    u32 width;
    u32 height;
    VkPresentModeKHR preferred_present_mode;
    const Mel_Alloc* alloc;
} Mel_Gpu_Swapchain_Opt;
```

All internal swapchain code that accesses `dev->surface` changes to use a stored surface reference instead.

### 4. `engine.h` / `engine.c` — becomes minimal platform init

No longer a god-object. Just:
```c
void mel__engine_init(void);    // backtrace, Vulkan library loading, platform init
void mel__engine_shutdown(void);
```

### 5. Game simplification

`game/main.c` transforms:
- `SDL_CreateWindow` → `mel_nwindow_init`
- Raw swapchain/device/frame management → `mel_ngpuview_init` + `mel_ngpuview_render`
- Image barriers + viewport + begin/end rendering → `mel_ngpuview_begin/end_pass`
- SDL event handling → stays for now (via `MEL_APP` macro), event abstraction is future work
- ImGui init that needs a window → needs thought (see open questions)

## Open Questions

### ImGui Integration
ImGui's SDL3 backend needs an `SDL_Window*`. The Vulkan backend needs the device, queue, descriptor pool, and swapchain format. With the new design:
- The gpu view has the swapchain format
- The device is shared
- But we don't have an `SDL_Window*` anymore for the SDL3 backend

Options:
- (a) Use ImGui's Metal backend instead of Vulkan backend on macOS (renders directly to the `CAMetalLayer`)
- (b) Keep SDL window creation for now, get the `SDL_Window*` from it for ImGui, but render into the gpu view
- (c) Write a minimal platform-specific ImGui backend that doesn't need SDL (imgui just needs key/mouse events and a rendering surface)
- (d) Defer ImGui integration to a later phase. Focus on getting the gpu view + device decoupling right first.

Recommendation: **(d)** for this phase. ImGui is editor-only and the editor system is Phase 3 material anyway. For now, the game can still use SDL's ImGui path alongside the gpu view.

### SDL Event Loop
The `MEL_APP` macro uses SDL's callback-based app lifecycle. Events come as `SDL_Event`. With the native UI system, events come through the platform's native event loop (NSApplication's run loop on macOS). These are two separate worlds.

For Phase 1, we keep `MEL_APP` + SDL events. Unifying the event system is future work. The gpu view renders when the app tells it to, regardless of event source.

### Vulkan Library Loading
Currently done via `SDL_Vulkan_LoadLibrary`. Without SDL in the GPU path, we need:
- macOS: `dlopen("libvulkan.dylib")` or link against MoltenVK directly
- Windows: `LoadLibrary("vulkan-1.dll")`
- Linux: `dlopen("libvulkan.so.1")`

This can live in `mel__engine_init()` or be a separate `mel_gpu_load_vulkan()` call.

### Present Queue Assumption
We assume graphics queue = present queue. Need to validate this assertion at surface creation time in the gpu view.

## Phased Implementation

### Phase 1A: GPU Device Decoupling (do now)
1. Remove `SDL_Window*` from `Mel_Gpu_Device_Opt`
2. Remove `VkSurfaceKHR` from `Mel_Gpu_Device`
3. Move surface creation out of device init
4. Add explicit `VkSurfaceKHR` to `Mel_Gpu_Swapchain_Opt`
5. Handle Vulkan instance extensions without SDL
6. Handle vkGetInstanceProcAddr without SDL
7. Update `game/main.c` to create surface separately (temporarily, via SDL still)
8. Verify build + run

### Phase 1B: Mel_NGpuView (do now)
1. Create `ui.native.gpuview.h` — struct, init, render, begin/end pass
2. Create `ui.native.gpuview.c` — C-side logic
3. Create `melody/osx/ui.native.gpuview.osx.m` — macOS backing (NSView + CAMetalLayer + VkSurfaceKHR)
4. Update `game/main.c` to use `Mel_NGpuView` instead of raw SDL window + manual swapchain
5. Update `nob.c` to build the new files
6. Verify build + run — same visual output, no validation errors

### Phase 2: Event & Window Unification (future)
- Unified event system wrapping SDL events and native events
- `Mel_NWindow` used for everything (GPU apps stop using SDL_CreateWindow)
- Remove SDL from game-facing code entirely

### Phase 3: Editor + ImGui Integration (future)
- ImGui as a gpu view feature (not global state)
- Editor viewports as gpu views inside a native window with native UI controls

## Files to Create
- `melody/ui.native.gpuview.h`
- `melody/ui.native.gpuview.c`
- `melody/osx/ui.native.gpuview.osx.m`

## Files to Modify
- `melody/gpu.device.h` — remove SDL_Window, remove VkSurfaceKHR from struct
- `melody/gpu.device.c` — remove surface creation, handle vkGetInstanceProcAddr without SDL, platform-specific instance extensions
- `melody/gpu.swapchain.h` — add VkSurfaceKHR to opt
- `melody/gpu.swapchain.c` — use opt surface instead of dev->surface
- `melody/engine.c` — Vulkan library loading
- `game/main.c` — use Mel_NGpuView
- `nob.c` — add new source files to build

## Verification
1. `cc -o nob nob.c && ./nob` — clean build, no warnings
2. `./nob run` — game runs, same visual output, no Vulkan validation errors
3. Resize works (gpu view handles swapchain recreation)
4. Escape still quits
5. Demos still compile and run (they don't touch GPU)
6. Clean shutdown — no leaked Vulkan objects
