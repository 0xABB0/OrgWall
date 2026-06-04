#pragma once

#include <webgpu/webgpu.h>

#include <core/types.h>
#include <allocator/allocator.h>
#include <collection.slotmap/slotmap.h>
#include <reactor/reactor.h>
#include <thread/mutex.h>
#include <thread/thread.h>
#include <debug/assert.h>

#include <gpu/handle.h>
#include <gpu/caps.h>
#include <gpu/device.h>
#include <gpu/future.h>
#include <gpu/threading.h>
#include <gpu/format.h>
#include <gpu/memory.h>
#include <gpu/queue.h>
#include <gpu/buffer.h>
#include <gpu/texture.h>
#include <gpu/sampler.h>
#include <gpu/binding.h>
#include <gpu/bind_group.h>
#include <gpu/state.h>
#include <gpu/shader.h>
#include <gpu/pipeline.h>
#include <gpu/sync.h>
#include <gpu/surface.h>
#include <gpu/swapchain.h>
#include <gpu/command.h>
#include <gpu/rendering.h>
#include <gpu/query.h>
#include <gpu/format_props.h>

struct Mel_Gpu_Adapter
{
    Mel_Gpu_Instance* instance;
    WGPUAdapter       wgpu;
    Mel_Gpu_Caps      caps;
};

struct Mel_Gpu_Instance
{
    Mel_Gpu_Debug_Config debug;
    const Mel_Alloc*     alloc;
    WGPUInstance         wgpu;
    Mel_Gpu_Adapter*     adapters;
    u32                  adapter_count;
};

typedef struct
{
    Mel_SlotMap map;
    bool        init;
} Mel_Gpu_Resource_Table;

typedef struct
{
    Mel_Gpu_Resource_Header header;
    WGPUBuffer              wgpu;
    usize                   size;
    void*                   shadow;
    bool                    host_visible;
    bool                    readback;
} Mel_Gpu_Buffer_Obj;

typedef struct
{
    Mel_Gpu_Resource_Header header;
    WGPUTexture            wgpu;
    WGPUTextureFormat      format;
    Mel_Gpu_Texture_Aspect aspect;
    u32                    width;
    u32                    height;
    u32                    depth;
    u32                    mip_levels;
    u32                    array_layers;
    u32                    sample_count;
} Mel_Gpu_Texture_Obj;

typedef struct
{
    Mel_Gpu_Resource_Header header;
    WGPUTextureView        wgpu;
    Mel_SlotMap_Handle     texture;
    WGPUTextureFormat      format;
    Mel_Gpu_Texture_Aspect aspect;
    u32                    base_mip;
    u32                    mip_count;
    u32                    base_layer;
    u32                    layer_count;
} Mel_Gpu_Texture_View_Obj;

typedef struct
{
    Mel_Gpu_Resource_Header header;
    WGPUSampler            wgpu;
} Mel_Gpu_Sampler_Obj;

typedef struct
{
    Mel_Gpu_Resource_Header header;
    WGPUShaderModule       vertex;
    WGPUShaderModule       fragment;
    WGPUShaderModule       compute;
    char*                  vertex_entry;
    char*                  fragment_entry;
    char*                  compute_entry;
} Mel_Gpu_Shader_Obj;

typedef struct
{
    Mel_Gpu_Resource_Header header;
    WGPURenderPipeline     render;
    WGPUComputePipeline    compute;
} Mel_Gpu_Pipeline_Obj;

struct Mel_Gpu_Device
{
    Mel_Gpu_Instance*        instance;
    Mel_Gpu_Adapter*         adapter;
    WGPUInstance             wgpu_instance;
    WGPUDevice               wgpu;
    WGPUQueue                queue;
    Mel_Gpu_Caps             caps;
    const Mel_Alloc*         alloc;
    Mel_Reactor*             reactor;
    Mel_Gpu_Completion_Pump* pump;
    Mel_Gpu_Thread_Tracker*  tracker;
    Mel_Gpu_Debug_Config     debug;
    Mel_Gpu_Device_Lost_Fn   on_device_lost;
    void*                    device_lost_user;
    bool                     lost;
    bool                     owns_instance;

    Mel_Mutex obj_lock;
    Mel_Mutex submit_lock;
    u64       submit_serial;
    u64       submit_completed;

    Mel_Gpu_Resource_Table buffers;
    Mel_Gpu_Resource_Table textures;
    Mel_Gpu_Resource_Table texture_views;
    Mel_Gpu_Resource_Table samplers;
    Mel_Gpu_Resource_Table shaders;
    Mel_Gpu_Resource_Table pipelines;
};

struct Mel_Gpu_Surface
{
    Mel_Gpu_Instance* instance;
    WGPUSurface       wgpu;
    void*             layer;
    void*             native;
    i32               width;
    i32               height;
};

struct Mel_Gpu_Command_List
{
    Mel_Gpu_Device*       dev;
    Mel_Gpu_Swapchain*    sc;
    WGPUCommandEncoder    encoder;
    WGPURenderPassEncoder pass;
    Mel_Gpu_Pipeline_Obj  bound;
    bool                  has_bound;
    bool                  standalone;
    bool                  recording;
    bool                  warned_unsupported;
};

struct Mel_Gpu_Queue
{
    Mel_Gpu_Device*    dev;
    Mel_Gpu_Queue_Role role;
    bool               locked_fallback;
};

struct Mel_Gpu_Swapchain
{
    Mel_Gpu_Device*   dev;
    Mel_Gpu_Surface*  surface;
    WGPUTextureFormat format;
    WGPUPresentMode   present_mode;
    u32               width;
    u32               height;
    bool              vsync;
    bool              configured;

    WGPUTexture     frame_texture;
    WGPUTextureView frame_view;
    bool            frame_ok;

    struct Mel_Gpu_Command_List recorder;
};

Mel_SlotMap_Handle mel_gpu__table_insert(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, const void* obj);
bool               mel_gpu__table_get_copy(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h, void* out);
void*              mel_gpu__table_get(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h);
bool               mel_gpu__table_alive(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h);
bool               mel_gpu__table_remove(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h);

void mel_gpu__track_enter(Mel_Gpu_Device* dev, const void* object, Mel_Gpu_Concurrency cls);
void mel_gpu__track_exit(Mel_Gpu_Device* dev, const void* object);

u64  mel_gpu__submit_serial_next(Mel_Gpu_Device* dev);
void mel_gpu__submit_complete(Mel_Gpu_Device* dev, u64 serial);

bool mel_gpu__instance_pump_tick(void* user);

bool mel_gpu__drain_until(WGPUInstance instance, const bool* done);
bool mel_gpu__drain_sync(Mel_Gpu_Device* dev, const bool* done, const char* what);

void              mel_gpu__caps_probe(WGPUAdapter adapter, Mel_Gpu_Caps* out);
WGPUTextureFormat mel_gpu__wgpu_format(Mel_Gpu_Format fmt);
Mel_Gpu_Format    mel_gpu__wgpu_format_to_mel(WGPUTextureFormat fmt);

WGPUStringView mel_gpu__sv(const char* s);

bool mel_gpu__buffer_get(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf, Mel_Gpu_Buffer_Obj* out);
bool mel_gpu__texture_get(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex, Mel_Gpu_Texture_Obj* out);
bool mel_gpu__texture_view_get(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View view, Mel_Gpu_Texture_View_Obj* out);
