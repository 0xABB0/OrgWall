#pragma once

#include "swapchain.fwd.h"
#include "gpu.types.h"
#include "gpu.device.fwd.h"
#include "gpu.cmd.fwd.h"
#include "window.fwd.h"

typedef struct Mel_Gpu_Submit_Gather Mel_Gpu_Submit_Gather;

struct Mel_Gpu_Submit_Gather {
    void* _wait_semaphore;
    u32 _wait_stage;
    void* _signal_semaphore;
    bool has_wait;
    bool has_signal;
};

struct Mel_Swapchain_Vtable {
    bool (*acquire)(Mel_Swapchain* sc, Mel_Gpu_Device* dev);
    void (*prepare_present)(Mel_Swapchain* sc, Mel_Gpu_Cmd* cmd);
    void (*collect_sync)(Mel_Swapchain* sc, Mel_Gpu_Submit_Gather* gather);
    void (*present)(Mel_Swapchain* sc, Mel_Gpu_Device* dev);
    void (*resize)(Mel_Swapchain* sc, Mel_Gpu_Device* dev, u32 width, u32 height);
    void (*shutdown)(Mel_Swapchain* sc, Mel_Gpu_Device* dev);
    Mel_Gpu_Image_Layout (*current_image_layout)(Mel_Swapchain* sc);
    Mel_Gpu_Present_Mode (*present_mode)(Mel_Swapchain* sc);
};

struct Mel_Swapchain {
    const Mel_Swapchain_Vtable* vtable;
    void* data;

    Mel_Gpu_Format format;
    u32 extent_width;
    u32 extent_height;
    u32 image_count;
    u32 current_image;
    void** _images;
    void** _image_views;
};

#define mel_swapchain_acquire(sc, dev)                    (sc)->vtable->acquire((sc), (dev))
#define mel_swapchain_prepare_present(sc, cmd)            do { if ((sc)->vtable->prepare_present) (sc)->vtable->prepare_present((sc), (cmd)); } while(0)
#define mel_swapchain_collect_sync(sc, gather)            do { if ((sc)->vtable->collect_sync) (sc)->vtable->collect_sync((sc), (gather)); } while(0)
#define mel_swapchain_present(sc, dev)                    do { if ((sc)->vtable->present) (sc)->vtable->present((sc), (dev)); } while(0)
#define mel_swapchain_resize(sc, dev, w, h)               (sc)->vtable->resize((sc), (dev), (w), (h))
#define mel_swapchain_shutdown(sc, dev)                    (sc)->vtable->shutdown((sc), (dev))

static inline Mel_Gpu_Image_Layout mel_swapchain_current_image_layout(Mel_Swapchain* sc)
{
    if (!sc->vtable->current_image_layout)
        return MEL_GPU_IMAGE_LAYOUT_UNDEFINED;
    return sc->vtable->current_image_layout(sc);
}

static inline Mel_Gpu_Present_Mode mel_swapchain_present_mode(Mel_Swapchain* sc)
{
    if (!sc->vtable->present_mode)
        return MEL_GPU_PRESENT_MODE_FIFO;
    return sc->vtable->present_mode(sc);
}

struct Mel_Swapchain_Entry {
    Mel_Swapchain swapchain;
    void* _surface;
    Mel_Window_Handle window;
    bool resize_requested;
};

Mel_Swapchain_Handle mel_swapchain_registry_insert(Mel_Swapchain_Entry* entry);
void                 mel_swapchain_registry_remove(Mel_Swapchain_Handle handle, Mel_Gpu_Device* dev);
Mel_Swapchain_Entry* mel_swapchain_registry_get(Mel_Swapchain_Handle handle);
Mel_Swapchain_Handle mel_swapchain_registry_find_by_window(Mel_Window_Handle window);
u32                  mel_swapchain_registry_count(void);
void                 mel_swapchain_registry_destroy_all(Mel_Gpu_Device* dev);
