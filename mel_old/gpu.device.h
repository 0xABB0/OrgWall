#pragma once

#include "core.types.h"
#include "allocator.h"
#include "str8.fwd.h"
#include "event.channel.fwd.h"
#include "gpu.pipeline_cache.fwd.h"

#include <SDL3/SDL.h>

typedef struct Mel_Gpu_Device Mel_Gpu_Device;
typedef struct Mel_Gpu_Capabilities Mel_Gpu_Capabilities;

struct Mel_Gpu_Capabilities {
    bool synchronization2;
    bool dynamic_rendering;
    bool buffer_device_address;
    bool descriptor_buffer;
    bool shader_draw_parameters;
    bool mesh_shader;
    bool descriptor_indexing;
    bool multi_draw_indirect;
    bool timestamp_queries;
    bool portability_subset;
    bool present_queue;
};

struct Mel_Gpu_Device {
    void* _backend;
    u32 graphics_family;
    u32 present_family;
    u32 transfer_family;
    Mel_Gpu_Capabilities capabilities;
    const Mel_Alloc* alloc;
    Mel_Gpu_Pipeline_Cache* pipeline_cache;
    bool validation_enabled;
    bool ready;
};

typedef struct {
    const Mel_Alloc* allocator;
    bool enable_validation;
    str8 app_name;
} Mel_Gpu_Device_Opt;

bool mel_gpu_device_init_opt(Mel_Gpu_Device* dev, Mel_Gpu_Device_Opt opt);
#define mel_gpu_device_init(dev, ...) mel_gpu_device_init_opt((dev), (Mel_Gpu_Device_Opt){__VA_ARGS__})

void mel_gpu_device_shutdown(Mel_Gpu_Device* dev);
void mel_gpu_device_wait_idle(Mel_Gpu_Device* dev);
Mel_Gpu_Capabilities mel_gpu_capabilities(Mel_Gpu_Device* dev);

void* mel_gpu_surface_create(Mel_Gpu_Device* dev, SDL_Window* window);
void  mel_gpu_surface_destroy(Mel_Gpu_Device* dev, void* surface);
bool  mel_gpu_device_configure_present(Mel_Gpu_Device* dev, void* surface);

Mel_Gpu_Device* mel_gpu_dev(void);

typedef struct Mel_Counter Mel_Counter;

typedef struct {
    Mel_Gpu_Device* dev;
    Mel_Counter* phase_counter;
} Mel_Gpu_Ready_Event;

extern Mel_Event_Channel mel_gpu_device_ready;
