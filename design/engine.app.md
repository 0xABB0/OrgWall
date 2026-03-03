# App Entry & Lifecycle

## Status

This document describes both the current and target (vNext) app entry.

Current implementation: `melody/core.app.h`, `melody/core.app.c`
Target implementation: link-based entry (no macro).

---

## App Entry (Link-Based)

The engine provides `main()`. The application implements well-known functions
that the engine calls at the right time. No macros.

### Current (implemented)

```c
MEL_APP(
    .on_init = my_init,
    .on_shutdown = my_shutdown,
    .on_update = my_update,
    .on_render = my_render,
    .on_event = my_event,
)
```

### Target (vNext)

The application implements two required functions:

```c
void app_init(void);
void app_shutdown(void);
```

And one optional function (weak definition — if not provided, default handling
applies: window close → quit, resize → swapchain recreate, etc.):

```c
void app_event(Mel_Event* event);
```

If `app_init` or `app_shutdown` are missing, the linker fails with a clear
error. No macro, no struct, no `Mel_App*` parameter. Just implement the
functions and link.

### Engine-Provided `main()`

```c
int main(int argc, char** argv) {
    mel_init();
    app_init();

    while (!mel_should_quit()) {
        mel_poll_events();
        mel__tick_simulations();
        mel__drive_windows();
    }

    app_shutdown();
    mel_shutdown();
}
```

`mel_init()` brings up all engine subsystems with sane defaults. `app_init`
then configures everything: mount VFS paths, create windows, create worlds,
create and register simulations. The application never touches the main loop,
frame boundaries, or window driving.

→ Frame pipeline details: `engine.frame.md`

### Quitting

```c
mel_quit();
```

---

## Manual Path

For fully custom setups — provide your own `main()` instead of linking against
the engine's. You call `mel_init`/`mel_shutdown` yourself and own the loop:

```c
int main() {
    mel_init();
    mel_vfs_mount(S8("data"), mel_vfs_backend_os());

    Mel_Window_Handle game   = mel_window_create(S8("Game"),   1280, 720);
    Mel_Window_Handle editor = mel_window_create(S8("Editor"), 1920, 1080);

    while (!mel_should_quit()) {
        mel_poll_events();
        mel_window_begin_frame(game);
        mel_window_end_frame(game);
        mel_window_begin_frame(editor);
        mel_window_end_frame(editor);
    }

    mel_window_destroy(editor);
    mel_window_destroy(game);
    mel_shutdown();
}
```

Both windows share the same texture pool, IO, VFS, GPU device.
The manual path gives full control over loop structure, window cadence,
and simulation ticking. The simulation registration system is not used — you
drive everything yourself.

### Headless (CLI)

Link against the headless entry point instead. Same pattern — `app_init`,
`app_shutdown`, simulations. No GPU, no windows, no swapchain init.

---

## mel_init / mel_shutdown

```c
typedef struct {
    u32 io_worker_count;
    u32 job_worker_count;
} Mel_Init_Opt;

void mel_init_opt(Mel_Init_Opt);
#define mel_init(...) mel_init_opt((Mel_Init_Opt){__VA_ARGS__})

void mel_shutdown(void);
```

### Init Order (`mel_init`)

1.  **Memory**: Tracking Allocator
2.  **Jobs**: Fiber worker threads (N-1 cores)
3.  **IO**: IO worker threads (separate pool)
4.  **CVars**: Register defaults (`r_vsync`, `sys_fps`, `t_timescale`)
5.  **VFS**: Initialize (no mounts — app mounts paths in `app_init`)
6.  **GPU Device**: Vulkan instance + physical device + logical device
7.  **Bindless**: Global texture descriptor array
8.  **Asset Pools**: texture, font_atlas, tileset, tilemap
9.  **Default Assets**: tex_white, tex_missing, tex_loading (synthetic)
10. **Input**: Action map registry

### Per-Simulation Init (`mel_sim_init`)

1.  **RNG**: Seeded PRNG state
2.  **Events**: Block allocator over caller-provided buffer
3.  **Tick Counter**: Starts at 0
4.  **Fixed Contexts**: Empty
5.  **Variable Updates**: Empty
6.  **Time Scale**: 1.0 (normal)
7.  **User Data**: From `Mel_Sim_Opt.user`

### Per-World Init (`mel_world_create`)

1.  **ECS**: World instance
2.  **Default Sync Systems**: Sprite, mesh, etc. → render list producers

### Per-Window Init (`mel_window_create`)

1.  **SDL Window**: Platform handle
2.  **Swapchain**: Tied to window surface
3.  **Render Frame**: Command pools, buffers, fences
4.  **Arenas**: N-buffered render arenas
5.  **Render Graph**: Default lists + default passes
6.  **ImGui**: Context for this window

### Shutdown Order

All windows destroyed first, then all worlds, then `mel_shutdown()` in
reverse of global init. Simulation contexts are caller-allocated — they go
away when their memory does. Tracking allocator shuts down last (reports
stats).
