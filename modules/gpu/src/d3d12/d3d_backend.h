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

// record.c — U17 state lowering (the load-bearing subset; unimplemented states fall to COMMON with a warn).
D3D12_RESOURCE_STATES mel_gpu__state_to_d3d12(Mel_Gpu_Resource_State state);
