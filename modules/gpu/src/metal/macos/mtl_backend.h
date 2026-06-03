#pragma once

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>

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
    Mel_Gpu_Instance*    instance;
    id<MTLDevice>        mtl;
    Mel_Gpu_Caps         caps;
};

struct Mel_Gpu_Instance
{
    Mel_Gpu_Debug_Config debug;
    const Mel_Alloc*     alloc;
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
    void*                   buf;
    usize                   size;
    bool                    host_visible;
} Mel_Gpu_Buffer_Obj;

typedef struct
{
    Mel_Gpu_Resource_Header header;
    void*                   texture;
    MTLPixelFormat          format;
    Mel_Gpu_Texture_Aspect  aspect;
    u32                     width;
    u32                     height;
    u32                     depth;
    u32                     mip_levels;
    u32                     array_layers;
    u32                     sample_count;
} Mel_Gpu_Texture_Obj;

typedef struct
{
    Mel_Gpu_Resource_Header header;
    void*                   view;
    Mel_SlotMap_Handle      texture;
    MTLPixelFormat          format;
    Mel_Gpu_Texture_Aspect  aspect;
    u32                     base_mip;
    u32                     mip_count;
    u32                     base_layer;
    u32                     layer_count;
} Mel_Gpu_Texture_View_Obj;

typedef struct
{
    Mel_Gpu_Resource_Header header;
    void*                   sampler;
} Mel_Gpu_Sampler_Obj;

struct Mel_Gpu_Device
{
    Mel_Gpu_Instance*        instance;
    Mel_Gpu_Adapter*         adapter;
    id<MTLDevice>            mtl;
    id<MTLCommandQueue>      queue;
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

    Mel_Mutex              obj_lock;
    Mel_Mutex             submit_lock;
    u64                   submit_serial;
    u64                   submit_completed;

    Mel_Gpu_Resource_Table buffers;
    Mel_Gpu_Resource_Table textures;
    Mel_Gpu_Resource_Table texture_views;
    Mel_Gpu_Resource_Table samplers;

    bool                     submit_poller_registered;
    struct Mel_Gpu_Pending*  pending;
    u32                      pending_count;
    u32                      pending_cap;
};

struct Mel_Gpu_Surface
{
    Mel_Gpu_Instance* instance;
    CAMetalLayer*     layer;
    void*             native;
    i32               width;
    i32               height;
};

struct Mel_Gpu_Command_List
{
    Mel_Gpu_Device*    dev;
    Mel_Gpu_Swapchain* sc;
    id<MTLCommandBuffer>        cb;
    id<MTLRenderCommandEncoder> encoder;
    bool               standalone;
    bool               recording;
    bool               warned_unsupported;
};

struct Mel_Gpu_Queue
{
    Mel_Gpu_Device*    dev;
    Mel_Gpu_Queue_Role role;
    bool               locked_fallback;
};

struct Mel_Gpu_Swapchain
{
    Mel_Gpu_Device*  dev;
    Mel_Gpu_Surface* surface;
    MTLPixelFormat   format;
    u32              width;
    u32              height;
    bool             vsync;

    id<CAMetalDrawable> drawable;
    u64                 frame_serial;
    bool                frame_ok;

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

void           mel_gpu__caps_probe(id<MTLDevice> mtl, Mel_Gpu_Caps* out);
MTLPixelFormat mel_gpu__mtl_format(Mel_Gpu_Format fmt);
Mel_Gpu_Format mel_gpu__mtl_format_to_mel(MTLPixelFormat fmt);

bool mel_gpu__buffer_get(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf, id<MTLBuffer>* out);
bool mel_gpu__texture_get(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex, Mel_Gpu_Texture_Obj* out);
bool mel_gpu__texture_view_get(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View view, Mel_Gpu_Texture_View_Obj* out);
