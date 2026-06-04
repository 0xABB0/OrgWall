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
    Mel_Gpu_Instance* instance;
    id<MTLDevice>     mtl;
    Mel_Gpu_Caps      caps;
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
    u32                     usage;
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
    u32                     usage;
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
    u32                     usage;
} Mel_Gpu_Texture_View_Obj;

typedef struct
{
    Mel_Gpu_Resource_Header header;
    void*                   sampler;
} Mel_Gpu_Sampler_Obj;

typedef struct
{
    Mel_Gpu_Resource_Header header;
    void*                   library;
    void*                   vs;
    void*                   fs;
    void*                   cs;
    char*                   vs_entry;
    char*                   fs_entry;
    char*                   cs_entry;
} Mel_Gpu_Shader_Obj;

typedef struct
{
    bool is_uniform;
    u32  host_offset;
    u32  arg_index;
    u32  size;
    u32  resource_kind;
} Mel_Gpu_Mtl_Arg_Field;

typedef struct
{
    Mel_Gpu_Resource_Header header;
    void*                   state;
    void*                   depth_stencil_state;
    Mel_Gpu_Topology        topology;
    bool                    compute;
    MTLSize                 threadgroup;
    MTLCullMode             cull_mode;
    MTLWinding              front_face;
    MTLTriangleFillMode     fill_mode;
    bool                    stencil_test;
    u32                     stencil_ref_front;
    u32                     stencil_ref_back;

    void*                  arg_encoder;
    usize                  arg_encoded_length;
    Mel_Gpu_Mtl_Arg_Field* arg_fields;
    u32                    arg_field_count;
    u32                    arg_host_size;
} Mel_Gpu_Pipeline_Obj;

enum
{
    MEL_GPU_BINDLESS_BINDING_SAMPLED_IMAGE = 0,
    MEL_GPU_BINDLESS_BINDING_SAMPLER = 1,
    MEL_GPU_BINDLESS_BINDING_STORAGE_BUFFER = 2,
    MEL_GPU_BINDLESS_BINDING_UNIFORM_BUFFER = 3,
    MEL_GPU_BINDLESS_BINDING_STORAGE_IMAGE = 4,
    MEL_GPU_BINDLESS_BINDING_COUNT = 5,
};

typedef struct
{
    bool      enabled;
    void*     heaps[MEL_GPU_BINDLESS_BINDING_COUNT];
    void**    resources[MEL_GPU_BINDLESS_BINDING_COUNT];
    u32       caps[MEL_GPU_BINDLESS_BINDING_COUNT];
    void*     residency;
    u32       cap_sampled_image;
    u32       cap_sampler;
    u32       cap_storage_buffer;
    u32       cap_uniform_buffer;
    u32       cap_storage_image;
    Mel_Mutex lock;
} Mel_Gpu_Bindless;

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

    Mel_Gpu_Bindless bindless;

    bool                    submit_poller_registered;
    struct Mel_Gpu_Pending* pending;
    u32                     pending_count;
    u32                     pending_cap;
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
    Mel_Gpu_Device*              dev;
    Mel_Gpu_Swapchain*           sc;
    id<MTLCommandBuffer>         cb;
    id<MTLRenderCommandEncoder>  encoder;
    id<MTLComputeCommandEncoder> compute_encoder;
    bool                         standalone;
    bool                         recording;
    bool                         warned_unsupported;

    MTLPrimitiveType primitive;
    bool             has_pipeline;
    id<MTLBuffer>    index_buffer;
    MTLIndexType     index_type;

    id<MTLComputePipelineState> compute_state;
    MTLSize                     compute_threadgroup;
    bool                        has_compute_pipeline;
    Mel_Gpu_Pipeline            compute_pipeline_handle;

    void* pc_stash;
    usize pc_stash_cap;
    usize pc_stash_len;
    bool  pc_stashed;
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

bool             mel_gpu__pipeline_get(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe, Mel_Gpu_Pipeline_Obj* out);
MTLPrimitiveType mel_gpu__topology_to_primitive(Mel_Gpu_Topology t);

void mel_gpu__cmd_end_active_encoder(Mel_Gpu_Command_List* cmd);

void mel_gpu__bindless_init(Mel_Gpu_Device* dev, bool want);
void mel_gpu__bindless_shutdown(Mel_Gpu_Device* dev);
bool mel_gpu__bindless_slot_fits(Mel_Gpu_Device* dev, u32 binding_class, u32 slot);
void mel_gpu__bindless_register_sampled_image(Mel_Gpu_Device* dev, u32 slot, id<MTLTexture> view);
void mel_gpu__bindless_register_storage_image(Mel_Gpu_Device* dev, u32 slot, id<MTLTexture> view);
void mel_gpu__bindless_register_storage_buffer(Mel_Gpu_Device* dev, u32 slot, id<MTLBuffer> buf);
void mel_gpu__bindless_register_uniform_buffer(Mel_Gpu_Device* dev, u32 slot, id<MTLBuffer> buf);
void mel_gpu__bindless_register_sampler(Mel_Gpu_Device* dev, u32 slot, id<MTLSamplerState> sampler);
void mel_gpu__bindless_bind_render(Mel_Gpu_Device* dev, id<MTLRenderCommandEncoder> enc);
void mel_gpu__bindless_bind_compute(Mel_Gpu_Device* dev, id<MTLComputeCommandEncoder> enc);

id<MTLResource>     mel_gpu__bindless_resource(Mel_Gpu_Device* dev, u32 binding_class, u32 slot);
id<MTLSamplerState> mel_gpu__bindless_sampler(Mel_Gpu_Device* dev, u32 slot);
u32                 mel_gpu__bindless_class_of_slang_kind(u32 slang_resource_kind);

#define MEL_GPU_METAL_VERTEX_BUFFER_INDEX        30u
#define MEL_GPU_METAL_PUSH_CONSTANT_INDEX        0u
#define MEL_GPU_METAL_VERTEX_BUFFER_BASE         30u
#define MEL_GPU_METAL_VERTEX_SLOT_TO_INDEX(slot) (MEL_GPU_METAL_VERTEX_BUFFER_BASE - (slot))

#define MEL_GPU_METAL_BINDLESS_HEAP_BASE         1u
#define MEL_GPU_METAL_BINDLESS_HEAP_INDEX(klass) (MEL_GPU_METAL_BINDLESS_HEAP_BASE + (klass))
