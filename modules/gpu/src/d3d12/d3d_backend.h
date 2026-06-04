#pragma once

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
#include <gpu/surface.h>
#include <gpu/swapchain.h>
#include <gpu/bind_group.h>

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
    Mel_Gpu_Buffer_Usage      usage;
    D3D12_GPU_VIRTUAL_ADDRESS gpu_va;
} Mel_Gpu_Buffer_Obj;

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
    Mel_SlotMap_Handle      texture;
    DXGI_FORMAT             format;
    Mel_Gpu_View_Dimension  dimension;
    u32                     base_mip;
    u32                     mip_count;
    u32                     base_layer;
    u32                     layer_count;
} Mel_Gpu_Texture_View_Obj;

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

typedef struct
{
    Mel_Gpu_Resource_Header          header;
    Mel_Gpu_Bind_Group_Layout_Entry* entries;
    u32                              entry_count;
    u32                              resource_descriptor_count;
    u32                              sampler_descriptor_count;
} Mel_Gpu_Bind_Group_Layout_Obj;

typedef struct
{
    Mel_Gpu_Resource_Header header;
    Mel_SlotMap_Handle      layout;
    u32                     resource_base;
    u32                     resource_count;
    u32                     sampler_base;
    u32                     sampler_count;
} Mel_Gpu_Bind_Group_Obj;

typedef struct
{
    char*          semantic;
    u32            semantic_index;
    Mel_Gpu_Format format;
    u32            input_register;
    u32            offset;
} Mel_Gpu_Dxil_Input;

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

typedef struct
{
    bool has_resource;
    u32  resource_param;
    bool has_sampler;
    u32  sampler_param;
} Mel_Gpu_Set_Param;

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
    u32*                     slot_strides;
    u32                      slot_stride_count;
    u32                      srv_table_param;
    u32                      smp_table_param;
    Mel_Gpu_Set_Param*       set_params;
    u32                      set_param_count;
    Mel_Gpu_Sampler*         static_samplers;
    u32                      static_sampler_count;
} Mel_Gpu_Pipeline_Obj;

typedef struct
{
    u32                    tex_index;
    u32                    tex_generation;
    u32                    mip;
    u32                    layer;
    Mel_Gpu_Resource_State state;
} Mel_Gpu_Cmd_State_Entry;

typedef struct
{
    u32 base;
    u32 count;
} Mel_Gpu_Classic_Block;

typedef struct
{
    u64                     marker;
    ID3D12Resource*         resource;
    ID3D12PipelineState*    pso;
    ID3D12RootSignature*    root_sig;
    Mel_Gpu_Resource_Table* reclaim_table;
    u32                     reclaim_index;
    bool                    has_reclaim;
    Mel_Gpu_Classic_Block   classic_res;
    Mel_Gpu_Classic_Block   classic_smp;
    bool                    has_classic_res;
    bool                    has_classic_smp;
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
    Mel_Gpu_Resource_Table bind_group_layouts;
    Mel_Gpu_Resource_Table bind_groups;

    Mel_Mutex              classic_lock;
    ID3D12DescriptorHeap*  classic_res_heap;
    u32                    classic_res_inc;
    u32                    classic_res_cap;
    u32                    classic_res_next;
    Mel_Gpu_Classic_Block* classic_res_free;
    u32                    classic_res_free_count;
    u32                    classic_res_free_cap;
    ID3D12DescriptorHeap*  classic_smp_heap;
    u32                    classic_smp_inc;
    u32                    classic_smp_cap;
    u32                    classic_smp_next;
    Mel_Gpu_Classic_Block* classic_smp_free;
    u32                    classic_smp_free_count;
    u32                    classic_smp_free_cap;

    Mel_Gpu_Sampler_Intern* sampler_interns;
    u32                     sampler_intern_count;
    u32                     sampler_intern_cap;

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

    Mel_Mutex             desc_lock;
    ID3D12DescriptorHeap* rtv_heap;
    u32                   rtv_size;
    u32                   rtv_cap;
    u32                   rtv_next;
    ID3D12DescriptorHeap* dsv_heap;
    u32                   dsv_size;
    u32                   dsv_cap;
    u32                   dsv_next;

    ID3D12CommandSignature* dispatch_indirect_sig;
    Mel_Mutex               dispatch_indirect_lock;

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

    Mel_Gpu_Swapchain* sc;

    Mel_Gpu_Cmd_State_Entry* states;
    u32                      state_count;
    u32                      state_cap;

    bool                  cur_compute;
    bool                  cur_bindless;
    u32                   cur_push_size;
    u32                   cur_vertex_stride;
    Mel_Gpu_Pipeline_Obj* cur_pipeline;
    bool                  classic_heaps_bound;
};

struct Mel_Gpu_Surface
{
    Mel_Gpu_Instance* instance;
    void*             hwnd;
    i32               width;
    i32               height;
};

struct Mel_Gpu_Swapchain
{
    Mel_Gpu_Device*  dev;
    Mel_Gpu_Surface* surface;
    IDXGISwapChain3* swap;
    DXGI_FORMAT      format;
    Mel_Gpu_Format   mel_format;
    u32              width;
    u32              height;
    bool             vsync;
    bool             allow_tearing;

    u32                   buffer_count;
    ID3D12Resource**      buffers;
    ID3D12DescriptorHeap* rtv_heap;
    u32                   rtv_inc;

    u32                         frames_in_flight;
    ID3D12CommandAllocator**    allocators;
    ID3D12GraphicsCommandList** lists;
    u64*                        frame_serial;
    u32                         frame_index;
    u32                         back_index;
    bool                        frame_ok;

    struct Mel_Gpu_Command_List recorder;
};

void mel_gpu__caps_from_adapter(IDXGIAdapter1* adapter, Mel_Gpu_Caps* out);
void mel_gpu__caps_refine_device(ID3D12Device* dev, ID3D12CommandQueue* queue, Mel_Gpu_Caps* out);

Mel_SlotMap_Handle mel_gpu__table_insert(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, const void* obj);
void*              mel_gpu__table_get(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h);
bool               mel_gpu__table_remove(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h);
bool               mel_gpu__table_remove_deferred(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h);
void               mel_gpu__table_reclaim(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, u32 index);
u64                mel_gpu__submit_serial_next(Mel_Gpu_Device* dev);
void               mel_gpu__submit_complete(Mel_Gpu_Device* dev, u64 serial);
void               mel_gpu__defer_free(Mel_Gpu_Device* dev, Mel_Gpu_Deferred_Free entry);
void               mel_gpu__wait_serial(Mel_Gpu_Device* dev, u64 serial);

DXGI_FORMAT mel_gpu__dxgi_format(Mel_Gpu_Format fmt);
bool        mel_gpu__texture_get(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex, Mel_Gpu_Texture_Obj** out);
bool        mel_gpu__texture_view_get(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View view, Mel_Gpu_Texture_View_Obj** out);
bool        mel_gpu__buffer_resource(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf, ID3D12Resource** out);

bool mel_gpu__buffer_get(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf, Mel_Gpu_Buffer_Obj** out);

D3D12_RESOURCE_STATES mel_gpu__state_to_d3d12(Mel_Gpu_Resource_State state);

bool mel_gpu__shader_get(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh, Mel_Gpu_Shader_Obj** out);

void mel_gpu__sampler_retain(Mel_Gpu_Device* dev, Mel_Gpu_Sampler s);
bool mel_gpu__sampler_desc(Mel_Gpu_Device* dev, Mel_Gpu_Sampler s, D3D12_SAMPLER_DESC* out);

void mel_gpu__dxil_reflect_inputs(const void* dxil, usize bytes, const Mel_Alloc* alloc, Mel_Gpu_Dxil_Input** out, u32* out_count, u32* out_stride);
void mel_gpu__dxil_inputs_free(const Mel_Alloc* alloc, Mel_Gpu_Dxil_Input* inputs, u32 count);

bool mel_gpu__device_is_lost(Mel_Gpu_Device* dev, HRESULT hr, const char* where);

void mel_gpu__bindless_init(Mel_Gpu_Device* dev);
void mel_gpu__bindless_destroy(Mel_Gpu_Device* dev);
void mel_gpu__bindless_register_texture_view(Mel_Gpu_Device* dev, u32 index, const Mel_Gpu_Texture_View_Obj* v);
void mel_gpu__bindless_register_storage_image(Mel_Gpu_Device* dev, u32 index, const Mel_Gpu_Texture_View_Obj* v);
void mel_gpu__bindless_register_buffer(Mel_Gpu_Device* dev, u32 index, const Mel_Gpu_Buffer_Obj* b, Mel_Gpu_Buffer_Usage usage);
void mel_gpu__bindless_register_sampler(Mel_Gpu_Device* dev, u32 index, const D3D12_SAMPLER_DESC* d);

D3D12_DESCRIPTOR_RANGE_TYPE mel_gpu__range_type(Mel_Gpu_Descriptor_Kind kind);
bool                        mel_gpu__descriptor_is_sampler(Mel_Gpu_Descriptor_Kind kind);
void                        mel_gpu__classic_init(Mel_Gpu_Device* dev);
void                        mel_gpu__classic_destroy(Mel_Gpu_Device* dev);
bool                        mel_gpu__classic_res_alloc(Mel_Gpu_Device* dev, u32 count, u32* out_base);
bool                        mel_gpu__classic_smp_alloc(Mel_Gpu_Device* dev, u32 count, u32* out_base);
void                        mel_gpu__classic_res_free(Mel_Gpu_Device* dev, Mel_Gpu_Classic_Block block);
void                        mel_gpu__classic_smp_free(Mel_Gpu_Device* dev, Mel_Gpu_Classic_Block block);
u32                         mel_gpu__classic_res_in_use(Mel_Gpu_Device* dev);
u32                         mel_gpu__classic_smp_in_use(Mel_Gpu_Device* dev);
D3D12_CPU_DESCRIPTOR_HANDLE mel_gpu__classic_res_cpu(Mel_Gpu_Device* dev, u32 slot);
D3D12_GPU_DESCRIPTOR_HANDLE mel_gpu__classic_res_gpu(Mel_Gpu_Device* dev, u32 slot);
D3D12_CPU_DESCRIPTOR_HANDLE mel_gpu__classic_smp_cpu(Mel_Gpu_Device* dev, u32 slot);
D3D12_GPU_DESCRIPTOR_HANDLE mel_gpu__classic_smp_gpu(Mel_Gpu_Device* dev, u32 slot);
bool                        mel_gpu__bind_group_layout_get(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group_Layout layout, Mel_Gpu_Bind_Group_Layout_Obj** out);
