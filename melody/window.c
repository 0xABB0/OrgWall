#include "window.h"
#include "swapchain.h"
#include "collection.slotmap.h"
#include "allocator.heap.h"
#include "string.str8.h"

typedef struct Mel_Window {
    SDL_Window* sdl;
} Mel_Window;

static Mel_SlotMap s_windows;
static bool s_initialized;

__attribute__((constructor(200)))
static void mel__window_registry_init(void)
{
    mel_slotmap_init(&s_windows, mel_alloc_heap(),
        .item_size = sizeof(Mel_Window), .initial_capacity = 4);
    s_initialized = true;
}

__attribute__((destructor(200)))
static void mel__window_registry_shutdown(void)
{
    if (!s_initialized) return;

    Mel_Window* windows = mel_slotmap_data(&s_windows);
    u32 count = mel_slotmap_count(&s_windows);

    for (u32 i = 0; i < count; i++)
        SDL_DestroyWindow(windows[i].sdl);

    mel_slotmap_free(&s_windows);
    s_initialized = false;
}

Mel_Window_Handle mel_window_create_opt(str8 title, Mel_Window_Create_Opt opt)
{
    assert(s_initialized);

    u32 w = opt.width > 0 ? opt.width : 1280;
    u32 h = opt.height > 0 ? opt.height : 720;
    u32 flags = opt.flags | SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE;

    char title_buf[256];
    if (!str8_is_empty(title))
        str8_to_buf(title, title_buf, sizeof(title_buf));
    else
        snprintf(title_buf, sizeof(title_buf), "Melody");

    SDL_Window* sdl = SDL_CreateWindow(title_buf, (int)w, (int)h, flags);
    if (!sdl)
    {
        SDL_Log("Failed to create window: %s", SDL_GetError());
        return MEL_WINDOW_HANDLE_NULL;
    }

    Mel_Window window = { .sdl = sdl };
    Mel_SlotMap_Handle raw = mel_slotmap_insert(&s_windows, &window);

    i32 pixel_w = 0;
    i32 pixel_h = 0;
    SDL_GetWindowSizeInPixels(sdl, &pixel_w, &pixel_h);

    SDL_DisplayID display = SDL_GetDisplayForWindow(sdl);
    float display_scale = SDL_GetWindowDisplayScale(sdl);
    const SDL_DisplayMode* mode = display ? SDL_GetCurrentDisplayMode(display) : nullptr;

    SDL_Log("Window created: \"%s\" logical=%ux%u pixels=%dx%d scale=%.2f display=%u refresh=%.2fHz",
        title_buf, w, h, pixel_w, pixel_h, display_scale, (u32)display,
        mode ? mode->refresh_rate : 0.0f);
    return (Mel_Window_Handle){ .handle = raw };
}

void mel_window_destroy(Mel_Window_Handle handle)
{
    assert(s_initialized);

    Mel_Window* w = mel_slotmap_get(&s_windows, handle.handle);
    assert(w != nullptr);

    Mel_Swapchain_Handle sc = mel_swapchain_registry_find_by_window(handle);
    if (mel_swapchain_handle_valid(sc))
        mel_swapchain_registry_remove(sc, nullptr);

    SDL_DestroyWindow(w->sdl);
    mel_slotmap_remove(&s_windows, handle.handle);
}

void mel_window_size(Mel_Window_Handle handle, i32* w, i32* h)
{
    assert(s_initialized);
    Mel_Window* win = mel_slotmap_get(&s_windows, handle.handle);
    assert(win != nullptr);
    SDL_GetWindowSize(win->sdl, w, h);
}

void mel_window_size_pixels(Mel_Window_Handle handle, i32* w, i32* h)
{
    assert(s_initialized);
    Mel_Window* win = mel_slotmap_get(&s_windows, handle.handle);
    assert(win != nullptr);
    SDL_GetWindowSizeInPixels(win->sdl, w, h);
}

u32 mel_window_id(Mel_Window_Handle handle)
{
    assert(s_initialized);
    Mel_Window* win = mel_slotmap_get(&s_windows, handle.handle);
    assert(win != nullptr);
    return SDL_GetWindowID(win->sdl);
}

u32 mel_window_count(void)
{
    return s_initialized ? mel_slotmap_count(&s_windows) : 0;
}

Mel_Swapchain_Handle mel_window_swapchain(Mel_Window_Handle handle)
{
    assert(s_initialized);
    return mel_swapchain_registry_find_by_window(handle);
}

SDL_Window* mel__window_sdl(Mel_Window_Handle handle)
{
    assert(s_initialized);
    Mel_Window* win = mel_slotmap_get(&s_windows, handle.handle);
    assert(win != nullptr);
    return win->sdl;
}

Mel_Window_Handle mel__window_find_by_id(u32 id)
{
    if (!s_initialized) return MEL_WINDOW_HANDLE_NULL;

    Mel_Window* windows = mel_slotmap_data(&s_windows);
    u32 count = mel_slotmap_count(&s_windows);

    for (u32 i = 0; i < count; i++)
    {
        if (SDL_GetWindowID(windows[i].sdl) != id)
            continue;

        u32 slot_idx = s_windows.packed_to_slot[i];
        Mel_SlotMap_Slot* slot = &s_windows.slots[slot_idx];
        return (Mel_Window_Handle){
            .handle = mel_slotmap_handle_make(slot_idx, slot->generation),
        };
    }

    return MEL_WINDOW_HANDLE_NULL;
}
