#pragma once

// D3D12 backend (gpu-rhi.md §12 M2 co-primary; design/gpu-d3d12.md). COM is consumed through the C
// struct/vtable path: d3d12.h selects it automatically when __cplusplus is undefined, and COBJMACROS gives
// the ID3D12X_Method(obj, ...) convenience macros. IID_* GUID symbols come from dxguid.lib.
#define COBJMACROS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <d3d12sdklayers.h>

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
#include <gpu/queue.h>
#include <gpu/memory.h>
#include <gpu/buffer.h>
#include <gpu/format.h>
#include <gpu/texture.h>
#include <gpu/state.h>
#include <gpu/command.h>
#include <gpu/rendering.h>

struct Mel_Gpu_Instance
{
    IDXGIFactory6*       factory;
    Mel_Gpu_Debug_Config debug;
    const Mel_Alloc*     alloc;
    Mel_Gpu_Adapter*     adapters;
    u32                  adapter_count;
    bool                 debug_layer;
};

struct Mel_Gpu_Adapter
{
    Mel_Gpu_Instance* instance;
    IDXGIAdapter1*    dxgi;
    Mel_Gpu_Caps      caps;
};

typedef struct
{
    Mel_SlotMap map;
    bool        init;
} Mel_Gpu_Resource_Table;

typedef struct
{
    Mel_Gpu_Resource_Header   header;
    ID3D12Resource*           resource;
    void*                     mapped;
    u64                       size;
    bool                      host_visible;
    Mel_Gpu_Buffer_Usage      usage; // U14: selects the heap class (STORAGE -> UAV, UNIFORM -> CBV)
    D3D12_GPU_VIRTUAL_ADDRESS gpu_va;
} Mel_Gpu_Buffer_Obj;

// U10 texture: a committed resource (dedicated-allocation floor). D3D12 views are descriptor-heap
// materializations, not objects, so the view records only intent (parent + format + dimension + range) and
// the RTV/SRV is created on demand into a heap at bind time (the co-primary contrast with VkImageView).
typedef struct
{
    Mel_Gpu_Resource_Header header;
    ID3D12Resource*         resource;
    DXGI_FORMAT             format;
    Mel_Gpu_Texture_Kind    kind;
    u32                     width;
    u32                     height;
    u32                     depth;
    u32                     mip_levels;
    u32                     array_layers;
    u32                     sample_count;
    Mel_Gpu_Texture_Usage   usage;
    bool                    is_depth;
} Mel_Gpu_Texture_Obj;

typedef struct
{
    Mel_Gpu_Resource_Header header;
    Mel_SlotMap_Handle      texture; // parent
    DXGI_FORMAT             format;
    Mel_Gpu_View_Dimension  dimension;
    u32                     base_mip;
    u32                     mip_count;
    u32                     base_layer;
    u32                     layer_count;
} Mel_Gpu_Texture_View_Obj;

// U11 sampler (gpu-rhi.md §6.3). A D3D12 sampler is a descriptor materialization, not a COM object, so the
// obj carries the D3D12_SAMPLER_DESC plus the dedup key/hash/refcount. Identical descriptors share one slot
// (auto-dedup, the public contract). The shader-visible sampler-heap slot == handle.index (§3.1 direct).
typedef struct
{
    u8  min_filter, mag_filter, mip_filter, wrap_u, wrap_v, wrap_w, compare, border;
    f32 max_anisotropy, lod_min, lod_max;
} Mel_Gpu_Sampler_Key;

typedef struct
{
    Mel_Gpu_Resource_Header header;
    D3D12_SAMPLER_DESC      desc;
    Mel_Gpu_Sampler_Key     key;
    u64                     hash;
    u32                     refcount;
} Mel_Gpu_Sampler_Obj;

typedef struct
{
    u64                hash;
    Mel_SlotMap_Handle handle;
} Mel_Gpu_Sampler_Intern;

// U12 reflection (gpu-rhi.md §6.4): one vertex-input element recovered from the DXIL container's input
// signature (ISG1/ISGN). D3D12 input layouts are keyed by semantic name + index, not by location as on
// Vulkan, so the reflected element carries the duplicated semantic string (freed at shader destroy).
typedef struct
{
    char*          semantic;
    u32            semantic_index;
    Mel_Gpu_Format format;
    u32            input_register;
    u32            offset;
} Mel_Gpu_Dxil_Input;

// U12 shader (gpu-rhi.md §6.4 raw-bytecode passthrough). The public Mel_Gpu_Shader_Bytecode_Opt fields are
// SPIR-V-named; on D3D12 they carry DXIL blobs (the public surface is backend-clean — flagged). The blobs
// are copied so the caller may free them. Reflection extracts only the input signature (the floor's
// reflection-default for vertex layout); push-constant size + bindless are explicit on the pipeline opt.
typedef struct
{
    Mel_Gpu_Resource_Header header;
    bool                    is_compute;
    void*                   vs;
    usize                   vs_size;
    void*                   fs;
    usize                   fs_size;
    void*                   cs;
    usize                   cs_size;
    Mel_Gpu_Dxil_Input*     inputs;
    u32                     input_count;
    u32                     vertex_stride;
} Mel_Gpu_Shader_Obj;

// U13 pipeline (gpu-rhi.md §6.5). The reflection-derived root signature + PSO. On bindless pipelines the
// root signature is root 32-bit constants (the per-draw record) plus the two heap-directly-indexed flags
// (SM 6.6 ResourceDescriptorHeap / SamplerDescriptorHeap); buffers, textures, and samplers are reached by
// descriptor index, never a raw GPU address (the §6.7 D3D12 contrast with Vulkan-BDA's mixed payload).
typedef struct
{
    Mel_Gpu_Resource_Header  header;
    ID3D12RootSignature*     root_sig;
    ID3D12PipelineState*     pso;
    bool                     bindless;
    bool                     is_compute;
    D3D12_PRIMITIVE_TOPOLOGY topology;
    u32                      push_constant_size;
    u32                      vertex_stride;
    // Root-parameter indices of the bindless descriptor tables (CBV/SRV/UAV heap + sampler heap), bound at
    // cmd_bind_pipeline. Constants (when present) are root param 0; the tables follow.
    u32                      srv_table_param;
    u32                      smp_table_param;
    Mel_Gpu_Sampler*         static_samplers;
    u32                      static_sampler_count;
} Mel_Gpu_Pipeline_Obj;

// U17 per-command-list, per-subresource state tracking (gpu-rhi.md §7.3): cmd_barrier validates the declared
// source state against what the list last recorded and asserts loudly on mismatch in debug.
typedef struct
{
    u32                    tex_index;
    u32                    tex_generation;
    u32                    mip;
    u32                    layer;
    Mel_Gpu_Resource_State state;
} Mel_Gpu_Cmd_State_Entry;

// U3 future-gated retirement (gpu-rhi.md §3.3): COM objects released only once submissions referencing them
// retire. No enum tag (MEL-CODE-001) — every non-null is freed.
typedef struct
{
    u64                     marker;
    ID3D12Resource*         resource;
    ID3D12PipelineState*    pso;
    ID3D12RootSignature*    root_sig;
    Mel_Gpu_Resource_Table* reclaim_table;
    u32                     reclaim_index;
    bool                    has_reclaim;
} Mel_Gpu_Deferred_Free;

typedef struct
{
    Mel_Gpu_Future* future;
    u64             serial;
} Mel_Gpu_Pending_Submit;

struct Mel_Gpu_Device
{
    Mel_Gpu_Instance*        instance;
    Mel_Gpu_Adapter*         adapter;
    ID3D12Device*            d3d;
    ID3D12CommandQueue*      direct_queue;
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

    ID3D12Fence* timeline;
    HANDLE       fence_event;

    Mel_Mutex              submit_lock;
    u64                    submit_serial;
    u64                    submit_completed;
    Mel_Gpu_Deferred_Free* deferred;
    u32                    deferred_count;
    u32                    deferred_cap;

    Mel_Gpu_Pending_Submit* pending;
    u32                     pending_count;
    u32                     pending_cap;
    bool                    submit_poller_registered;

    Mel_Mutex              obj_lock;
    Mel_Gpu_Resource_Table buffers;
    Mel_Gpu_Resource_Table textures;
    Mel_Gpu_Resource_Table texture_views;
    Mel_Gpu_Resource_Table samplers;
    Mel_Gpu_Resource_Table shaders;
    Mel_Gpu_Resource_Table pipelines;

    // U11 sampler dedup interns (hash -> handle), guarded by obj_lock.
    Mel_Gpu_Sampler_Intern* sampler_interns;
    u32                     sampler_intern_count;
    u32                     sampler_intern_cap;

    // U14 bindless shader-visible heaps (gpu-rhi.md §6.7). One CBV/SRV/UAV heap partitioned into per-class
    // base offsets (SRV textures / UAV storage buffers / CBV uniform buffers / UAV storage images) + one
    // sampler heap. Unlike the Vulkan floor's per-class descriptor arrays (where slot == handle.index in
    // every class), D3D12's ResourceDescriptorHeap is one flat heap, so the heap slot is base[class] +
    // handle.index and is always queried via mel_gpu_*_bindless_slot (§3.1). Created at device-create when
    // descriptor_indexing is requested and the bindless tier is full.
    bool                  bindless_enabled;
    ID3D12DescriptorHeap* srv_heap;
    u32                   srv_inc;
    u32                   srv_cap;
    ID3D12DescriptorHeap* smp_heap;
    u32                   smp_inc;
    u32                   smp_cap;
    u32                   cap_sampled_image;
    u32                   cap_storage_buffer;
    u32                   cap_uniform_buffer;
    u32                   cap_storage_image;
    u32                   base_sampled_image;
    u32                   base_storage_buffer;
    u32                   base_uniform_buffer;
    u32                   base_storage_image;

    // U16: CPU-only RTV/DSV descriptor heaps, allocated round-robin per begin_rendering (the descriptor is
    // consumed at OMSetRenderTargets record time, so the slot is immediately reusable).
    Mel_Mutex             desc_lock;
    ID3D12DescriptorHeap* rtv_heap;
    u32                   rtv_size;
    u32                   rtv_cap;
    u32                   rtv_next;
    ID3D12DescriptorHeap* dsv_heap;
    u32                   dsv_size;
    u32                   dsv_cap;
    u32                   dsv_next;

    void (*budget_pressure_cb)(struct Mel_Gpu_Device*, Mel_Gpu_Memory_Budget, void*);
    void* budget_pressure_user;
};

struct Mel_Gpu_Queue
{
    Mel_Gpu_Device*     dev;
    ID3D12CommandQueue* d3d;
    Mel_Gpu_Queue_Role  role;
    bool                internally_synchronized;
    bool                locked_fallback;
};

struct Mel_Gpu_Command_List
{
    Mel_Gpu_Device*            dev;
    ID3D12CommandAllocator*    allocator;
    ID3D12GraphicsCommandList* list;
    bool                       recording;

    Mel_Gpu_Cmd_State_Entry* states;
    u32                      state_count;
    u32                      state_cap;

    // U13 currently-bound pipeline state — set at cmd_bind_pipeline, consumed by push-constants / draw /
    // vertex-buffer recording so graphics and compute share one path.
    bool cur_compute;
    bool cur_bindless;
    u32  cur_push_size;
    u32  cur_vertex_stride;
};

// caps.c
void mel_gpu__caps_from_adapter(IDXGIAdapter1* adapter, Mel_Gpu_Caps* out);
void mel_gpu__caps_refine_device(ID3D12Device* dev, ID3D12CommandQueue* queue, Mel_Gpu_Caps* out);

// device.c — U1 table helpers + U3 watermark.
Mel_SlotMap_Handle mel_gpu__table_insert(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, const void* obj);
void*              mel_gpu__table_get(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h);
bool               mel_gpu__table_remove(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h);
bool               mel_gpu__table_remove_deferred(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h);
void               mel_gpu__table_reclaim(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, u32 index);
u64                mel_gpu__submit_serial_next(Mel_Gpu_Device* dev);
void               mel_gpu__submit_complete(Mel_Gpu_Device* dev, u64 serial);
void               mel_gpu__defer_free(Mel_Gpu_Device* dev, Mel_Gpu_Deferred_Free entry);
void               mel_gpu__wait_serial(Mel_Gpu_Device* dev, u64 serial);

// texture.c
DXGI_FORMAT mel_gpu__dxgi_format(Mel_Gpu_Format fmt);
bool        mel_gpu__texture_get(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex, Mel_Gpu_Texture_Obj** out);
bool        mel_gpu__texture_view_get(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View view, Mel_Gpu_Texture_View_Obj** out);
bool        mel_gpu__buffer_resource(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf, ID3D12Resource** out);

// buffer.c
bool mel_gpu__buffer_get(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf, Mel_Gpu_Buffer_Obj** out);

// record.c — U17 state lowering (the load-bearing subset; unimplemented states fall to COMMON with a warn).
D3D12_RESOURCE_STATES mel_gpu__state_to_d3d12(Mel_Gpu_Resource_State state);

// shader.c
bool mel_gpu__shader_get(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh, Mel_Gpu_Shader_Obj** out);

// sampler.c — static-sampler lifetime (pipeline retains a claim) + descriptor lookup for the root signature.
void mel_gpu__sampler_retain(Mel_Gpu_Device* dev, Mel_Gpu_Sampler s);
bool mel_gpu__sampler_desc(Mel_Gpu_Device* dev, Mel_Gpu_Sampler s, D3D12_SAMPLER_DESC* out);

// reflect.c — DXIL container reader: input signature (ISG1/ISGN) -> reflected vertex-input elements.
void mel_gpu__dxil_reflect_inputs(const void* dxil, usize bytes, const Mel_Alloc* alloc, Mel_Gpu_Dxil_Input** out, u32* out_count, u32* out_stride);
void mel_gpu__dxil_inputs_free(const Mel_Alloc* alloc, Mel_Gpu_Dxil_Input* inputs, u32 count);

// binding.c — U14 bindless heaps.
void mel_gpu__bindless_init(Mel_Gpu_Device* dev);
void mel_gpu__bindless_destroy(Mel_Gpu_Device* dev);
void mel_gpu__bindless_register_texture_view(Mel_Gpu_Device* dev, u32 index, const Mel_Gpu_Texture_View_Obj* v);
void mel_gpu__bindless_register_buffer(Mel_Gpu_Device* dev, u32 index, const Mel_Gpu_Buffer_Obj* b, Mel_Gpu_Buffer_Usage usage);
void mel_gpu__bindless_register_sampler(Mel_Gpu_Device* dev, u32 index, const D3D12_SAMPLER_DESC* d);
