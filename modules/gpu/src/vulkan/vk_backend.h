#pragma once

#include <vulkan/vulkan.h>

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
#include <gpu/queue.h>
#include <gpu/memory.h>
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

struct Mel_Gpu_Instance
{
    VkInstance               vk;
    Mel_Gpu_Debug_Config     debug;
    VkDebugUtilsMessengerEXT messenger;
    const Mel_Alloc*         alloc;
    Mel_Gpu_Adapter*         adapters;
    u32                      adapter_count;
    bool                     portability;
};

struct Mel_Gpu_Adapter
{
    Mel_Gpu_Instance* instance;
    VkPhysicalDevice  phys;
    Mel_Gpu_Caps      caps;
};

typedef struct
{
    Mel_SlotMap map;
    bool        init;
} Mel_Gpu_Resource_Table;

typedef struct Mel_Gpu_Allocator Mel_Gpu_Allocator;

typedef struct
{
    VkDeviceMemory mem;
    VkDeviceSize   offset;
    VkDeviceSize   size;
    void*          mapped;
    void*          block;
} Mel_Gpu_Allocation;

typedef struct
{
    Mel_Gpu_Resource_Header header;
    VkBuffer                buf;
    Mel_Gpu_Allocation      alloc;
    usize                   size;
    bool                    host_visible;
} Mel_Gpu_Buffer_Obj;

typedef struct
{
    Mel_Gpu_Resource_Header header;
    VkImage                 image;
    Mel_Gpu_Allocation      alloc;
    VkFormat                format;
    VkImageAspectFlags      aspect;
    VkImageType             image_type;
    u32                     width;
    u32                     height;
    u32                     depth;
    u32                     mip_levels;
    u32                     array_layers;
    u32                     sample_count;
    VkImageUsageFlags       usage;
} Mel_Gpu_Texture_Obj;

typedef struct
{
    Mel_Gpu_Resource_Header header;
    VkImageView             view;
    Mel_SlotMap_Handle      texture;
    VkFormat                format;
    VkImageAspectFlags      aspect;
    u32                     base_mip;
    u32                     mip_count;
    u32                     base_layer;
    u32                     layer_count;
} Mel_Gpu_Texture_View_Obj;

// U11: the canonical (resolved) sampler descriptor — the dedup key. No internal padding holes (8 bytes of
// u8 then three f32), so it hashes and compares by raw bytes.
typedef struct
{
    u8  min_filter, mag_filter, mip_filter, wrap_u, wrap_v, wrap_w, compare, border;
    f32 max_anisotropy, lod_min, lod_max;
} Mel_Gpu_Sampler_Key;

// An interned sampler. `refcount` is the number of logical claims sharing this descriptor (dedup,
// gpu-rhi.md §6.3); `hash`/`key` key the per-device intern table. The bindless slot is the handle index.
typedef struct
{
    Mel_Gpu_Resource_Header header;
    VkSampler               sampler;
    Mel_Gpu_Sampler_Key     key;
    u64                     hash;
    u32                     refcount;
} Mel_Gpu_Sampler_Obj;

// U12 reflection (gpu-rhi.md §6.4): what the engine derives from a SPIR-V blob so U13 can build the
// pipeline layout without hand-declared sizes. The shader is the source of truth for its bindings.

// A descriptor the shader declares at set 0 (the engine-canonical bindless-heap set). The binding index
// IS the heap class index (sampled image=0, sampler=1, storage buffer=2, uniform buffer=3, storage
// image=4), so the heap cap for a binding is looked up by binding number. `runtime_array` is the
// update-after-bind heap signature; a sized `array_len` exceeding the heap's class cap is MissingBindlessSlot.
typedef struct
{
    u32  binding;
    u32  array_len;     // declared length of a sized array; 1 for a single descriptor
    bool runtime_array; // OpTypeRuntimeArray (unbounded) — the bindless-heap signature
} Mel_Gpu_Reflect_Set0_Binding;

// A reflected vertex-input attribute (vertex stage only); offsets are tight-packed in ascending-location
// order into a single interleaved binding (gpu-rhi.md §6.5 reflection default, manual override).
typedef struct
{
    u32            location;
    Mel_Gpu_Format format;
    u32            offset;
} Mel_Gpu_Reflect_Vertex_Attr;

// A reflected specialization constant (gpu-rhi.md §6.4). `id` is the SpecId; `bytes` its scalar size.
typedef struct
{
    u32 id;
    u32 bytes;
} Mel_Gpu_Reflect_Spec_Constant;

typedef struct
{
    u32  push_constant_size; // bytes spanned by the PushConstant block; 0 if none
    bool uses_bindless_set;  // set 0 declares >=1 runtime descriptor array (the heap signature)

    Mel_Gpu_Reflect_Set0_Binding* set0; // set-0 descriptors (union over stages, deduped by binding)
    u32                           set0_count;

    Mel_Gpu_Reflect_Vertex_Attr* vertex_attrs; // vertex stage only, sorted by location
    u32                          vertex_attr_count;
    u32                          vertex_stride;

    Mel_Gpu_Reflect_Spec_Constant* spec_constants; // union over stages, deduped by id
    u32                            spec_constant_count;

    const Mel_Alloc* alloc; // owns the three arrays above; released by mel_gpu__reflection_free
} Mel_Gpu_Spirv_Reflection;

typedef struct
{
    Mel_Gpu_Resource_Header  header;
    VkShaderModule           vs;
    VkShaderModule           fs;
    VkShaderModule           cs; // U13 compute single-stage; VK_NULL_HANDLE for a graphics shader
    char*                    vs_entry;
    char*                    fs_entry;
    char*                    cs_entry;
    Mel_Gpu_Spirv_Reflection reflection; // union of the vs+fs reflections, or the cs reflection
} Mel_Gpu_Shader_Obj;

typedef struct
{
    Mel_Gpu_Resource_Header header;
    VkPipeline              pipeline;
    VkPipelineLayout        layout;
    VkDescriptorSetLayout   static_sampler_layout; // U11 immutable samplers, owned; VK_NULL_HANDLE if none
    bool                    bindless;              // U14: set 0 is the device bindless heap
    VkPipelineBindPoint     bind_point;            // U13: GRAPHICS or COMPUTE — drives heap/descriptor binds
    VkShaderStageFlags      pc_stages;             // push-constant stages for cmd_push_constants
    // U11/U13: a refcount claim per static sampler, held for the pipeline's lifetime and released at destroy
    // (gpu-rhi.md §6.3). Owned copy of the handles; NULL when the pipeline has no static samplers.
    Mel_Gpu_Sampler*        static_samplers;
    u32                     static_sampler_count;
} Mel_Gpu_Pipeline_Obj;

// U14 classic descriptor-set path (gpu-rhi.md §6.7). A bind-group layout owns one VkDescriptorSetLayout plus
// a copy of its entries (so writes can pick the VkDescriptorType and validate the binding). A bind group
// owns one VkDescriptorSet allocated from the device classic-pool chain.
typedef struct
{
    Mel_Gpu_Resource_Header           header;
    VkDescriptorSetLayout             layout;
    Mel_Gpu_Bind_Group_Layout_Entry*  entries;
    u32                               entry_count;
} Mel_Gpu_Bind_Group_Layout_Obj;

typedef struct
{
    Mel_Gpu_Resource_Header header;
    VkDescriptorSet         set;
    VkDescriptorPool        pool;        // the pool this set was allocated from, for vkFreeDescriptorSets
    Mel_SlotMap_Handle      layout;      // the source bind-group layout (for write-time kind lookup)
} Mel_Gpu_Bind_Group_Obj;

typedef struct
{
    Mel_Gpu_Resource_Header header;
    VkSemaphore             semaphore;
    VkFence                 fence;
    bool                    is_timeline;
} Mel_Gpu_Sync_Obj;

typedef struct
{
    VkFence         fence;
    Mel_Gpu_Future* future;
    u64             serial;
    bool            active;
} Mel_Gpu_Pending_Submit;

typedef struct
{
    Mel_Thread_Id thread;
    u32           family;
    VkCommandPool pool;
} Mel_Gpu_Thread_Pool;

// U3 retirement: a destroyed resource's Vulkan objects are freed only once every submission that could
// reference it has completed (gpu-rhi.md §3.3). No enum tag (MEL-CODE-001) — every non-null handle is freed.
typedef struct
{
    u64                   marker;
    VkImage               image;
    VkImageView           view;
    VkBuffer              buffer;
    VkPipeline            pipeline;
    VkPipelineLayout      pipeline_layout;
    VkDescriptorSetLayout descriptor_set_layout;
    VkSampler             sampler;
    VkShaderModule        shader_vs;
    VkShaderModule        shader_fs;
    // U14 classic bind group: free the descriptor set back to its pool once in-flight submissions retire.
    VkDescriptorSet       descriptor_set;
    VkDescriptorPool      descriptor_set_pool;
    Mel_Gpu_Allocation    alloc;
    bool                  has_alloc;
    // U14: future-gated slot reclamation (gpu-rhi.md §3.3). The slotmap index is withheld until this entry
    // retires, so a heap-registered resource's slot is never reused while an in-flight submission reads it.
    Mel_Gpu_Resource_Table* reclaim_table;
    u32                     reclaim_index;
    bool                    has_reclaim;
} Mel_Gpu_Deferred_Free;

// U17: per-command-list, per-subresource state tracking (gpu-rhi.md §7.3). cmd_barrier validates the
// declared source state against the tracked state and asserts loudly on mismatch in debug.
typedef struct
{
    u32                    tex_index;
    u32                    tex_generation;
    u32                    mip;
    u32                    layer;
    Mel_Gpu_Resource_State state;
} Mel_Gpu_Cmd_State_Entry;

// U14: the device-global bindless heap (gpu-rhi.md §6.7). One descriptor set, one large partially-bound
// update-after-bind array per resource class. The per-class binding index is fixed and engine-canonical;
// every bindless pipeline reflects this same set 0 layout. A resource's slot is its slotmap handle index
// (the §3.1 direct contract), written into the array at creation time.
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
    bool                  enabled;
    VkDescriptorPool      pool;
    VkDescriptorSetLayout set_layout;
    VkDescriptorSet       set;
    u32                   cap_sampled_image;
    u32                   cap_sampler;
    u32                   cap_storage_buffer;
    u32                   cap_uniform_buffer;
    u32                   cap_storage_image;
    Mel_Mutex             lock; // descriptor-heap writes are SerializedPerObject per heap region (§3.7)
} Mel_Gpu_Bindless;

// U11: per-device sampler intern table — maps a descriptor hash to the shared handle (gpu-rhi.md §6.3).
typedef struct
{
    u64                hash;
    Mel_SlotMap_Handle handle;
} Mel_Gpu_Sampler_Intern;

struct Mel_Gpu_Device
{
    Mel_Gpu_Instance*        instance;
    Mel_Gpu_Adapter*         adapter;
    VkPhysicalDevice         phys;
    VkDevice                 vk;
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
    bool                     has_memory_budget;

    void (*budget_pressure_cb)(struct Mel_Gpu_Device*, Mel_Gpu_Memory_Budget, void*);
    void* budget_pressure_user;

    u32     graphics_family;
    VkQueue graphics_queue;
    Mel_Mutex submit_lock;

    VkPhysicalDeviceMemoryProperties mem_props;
    Mel_Gpu_Allocator*               allocator;
    f32                              max_sampler_anisotropy; // 1.0 when samplerAnisotropy not enabled (U11)
    bool                             bda_enabled;            // U14 ceiling: buffer_device_address granted

    // U13 render state (gpu-rhi.md §6.5): optional rasterizer/blend features enabled at device-create when the
    // physical device supports them; the pipeline path degrades a request for an unenabled one with a warning.
    bool               feat_fill_non_solid;      // wireframe / point polygon modes
    bool               feat_depth_bounds;        // depth-bounds test
    bool               feat_depth_bias_clamp;    // non-zero depth-bias clamp
    bool               feat_sample_rate_shading; // sample shading + min-sample-shading
    VkSampleCountFlags fb_color_samples;         // framebufferColorSampleCounts limit (MSAA validation)
    VkSampleCountFlags fb_depth_samples;         // framebufferDepthSampleCounts limit

    Mel_Mutex              obj_lock;
    Mel_Gpu_Resource_Table buffers;
    Mel_Gpu_Resource_Table textures;
    Mel_Gpu_Resource_Table texture_views;
    Mel_Gpu_Resource_Table samplers;
    Mel_Gpu_Resource_Table shaders;
    Mel_Gpu_Resource_Table pipelines;
    Mel_Gpu_Resource_Table syncs;
    Mel_Gpu_Resource_Table bind_group_layouts; // U14 classic path
    Mel_Gpu_Resource_Table bind_groups;        // U14 classic path

    // U14 classic descriptor-set path: a grown-on-demand chain of pools to allocate bind-group sets from.
    Mel_Mutex         classic_pool_lock;
    VkDescriptorPool* classic_pools;
    u32               classic_pool_count;
    u32               classic_pool_cap;

    // U14 bindless heap + U11 sampler dedup. Guarded by bindless.lock / sampler_lock respectively.
    Mel_Gpu_Bindless        bindless;
    Mel_Mutex               sampler_lock;
    Mel_Gpu_Sampler_Intern* sampler_interns;
    u32                     sampler_intern_count;
    u32                     sampler_intern_cap;

    Mel_Gpu_Pending_Submit* pending;
    u32                     pending_count;
    u32                     pending_cap;
    bool                    submit_poller_registered;

    // U3 future-gated retirement watermark, fed by every submission path (queue_submit + swapchain frames).
    u64                    submit_serial;
    u64                    submit_completed;
    Mel_Gpu_Deferred_Free* deferred;
    u32                    deferred_count;
    u32                    deferred_cap;

    Mel_Mutex            pool_lock;
    Mel_Gpu_Thread_Pool* thread_pools;
    u32                  thread_pool_count;
    u32                  thread_pool_cap;

    bool                       dynamic_rendering;
    PFN_vkCmdBeginRenderingKHR  cmd_begin_rendering;
    PFN_vkCmdEndRenderingKHR    cmd_end_rendering;

    // U17 (gpu-rhi.md §7.3): VK_KHR_synchronization2 re-lowers barriers onto vkCmdPipelineBarrier2 with the
    // pipeline_stage_2 / access_2 enums; the legacy vkCmdPipelineBarrier path is the §7.3 floor when ungranted.
    bool                        sync2;
    PFN_vkCmdPipelineBarrier2KHR cmd_pipeline_barrier2;
};

struct Mel_Gpu_Surface
{
    Mel_Gpu_Instance* instance;
    VkSurfaceKHR      vk;
    void*             metal_layer;
    void*             native;
    i32               width;
    i32               height;
};

struct Mel_Gpu_Command_List
{
    Mel_Gpu_Device*    dev;
    VkCommandBuffer    cb;
    Mel_Gpu_Swapchain* sc;
    VkPipelineLayout   cur_layout;
    VkPipelineBindPoint cur_bind_point; // U13: set at cmd_bind_pipeline; drives heap/descriptor-set binds
    VkShaderStageFlags  cur_pc_stages;  // push-constant stages of the bound pipeline
    VkCommandPool      owner_pool;
    bool               standalone;
    bool               recording;

    Mel_Gpu_Cmd_State_Entry* states;
    u32                      state_count;
    u32                      state_cap;
};

struct Mel_Gpu_Queue
{
    Mel_Gpu_Device*        dev;
    VkQueue                vk;
    u32                    family;
    Mel_Gpu_Queue_Role     role;
    bool                   internally_synchronized;
    bool                   locked_fallback;
};

struct Mel_Gpu_Swapchain
{
    Mel_Gpu_Device*  dev;
    Mel_Gpu_Surface* surface;
    VkSwapchainKHR   vk;
    VkFormat         format;
    VkColorSpaceKHR  color_space;
    VkExtent2D       extent;
    bool             vsync;

    u32            image_count;
    VkImage*       images;
    VkImageView*   views;
    VkFramebuffer* framebuffers;
    VkRenderPass   render_pass;

    u32              frames_in_flight;
    VkCommandPool    cmd_pool;
    VkCommandBuffer* cmd_buffers;
    VkSemaphore*     image_available;
    VkSemaphore*     render_finished;
    VkFence*         in_flight;
    u64*             frame_serial;
    u32              frame_index;
    u32              current_image;
    bool             frame_ok;

    struct Mel_Gpu_Command_List recorder;
};

const char* mel_gpu__vk_result_str(VkResult r);
void        mel_gpu__caps_probe(VkPhysicalDevice phys, Mel_Gpu_Caps* out);

VkDebugUtilsMessengerCreateInfoEXT mel_gpu__debug_messenger_info(void);
void                               mel_gpu__debug_messenger_create(VkInstance instance, VkDebugUtilsMessengerEXT* out);
void                               mel_gpu__debug_messenger_destroy(VkInstance instance, VkDebugUtilsMessengerEXT messenger);
bool        mel_gpu__device_is_lost(Mel_Gpu_Device* dev, VkResult r, const char* where);

VkSurfaceKHR mel_gpu__vk_create_metal_surface(VkInstance instance, void* native_view, void** out_layer);
void         mel_gpu__vk_metal_layer_set_size(void* layer, i32 width, i32 height);
void         mel_gpu__vk_metal_layer_release(void* layer);
VkSurfaceKHR mel_gpu__vk_create_win32_surface(VkInstance instance, void* hwnd); // U18 §7.4 (src/vulkan/windows)

VkFormat       mel_gpu__vk_format(Mel_Gpu_Format fmt);
Mel_Gpu_Format mel_gpu__vk_format_to_mel(VkFormat fmt);
VkImageAspectFlags mel_gpu__aspect_flags(Mel_Gpu_Texture_Aspect aspect, VkFormat fmt);
// Shared comparison-op lowering (sampler compare + U13 depth/stencil compare). NONE maps to NEVER.
VkCompareOp mel_gpu__vk_compare_op(Mel_Gpu_Compare_Op c);

u32 mel_gpu__vk_find_memory_type(Mel_Gpu_Device* dev, u32 type_bits, VkMemoryPropertyFlags props);

VkCommandPool mel_gpu__thread_pool(Mel_Gpu_Device* dev, u32 family);

// U3 future-gated retirement. submit_serial_next reserves the id of a submission about to be made;
// submit_complete advances the watermark and frees every deferred resource gated at or below it.
u64  mel_gpu__submit_serial_next(Mel_Gpu_Device* dev);
void mel_gpu__submit_complete(Mel_Gpu_Device* dev, u64 serial);
void mel_gpu__defer_free(Mel_Gpu_Device* dev, Mel_Gpu_Deferred_Free entry);

// U17: maps a Mel_Gpu_Resource_State to the Vulkan (stage, access, layout) triple for legacy barriers.
void mel_gpu__state_to_barrier(Mel_Gpu_Resource_State state, bool is_depth, VkPipelineStageFlags* stage, VkAccessFlags* access, VkImageLayout* layout);
// U17 (gpu-rhi.md §7.3): the synchronization2 peer — pipeline_stage_2 / access_2 / layout for vkCmdPipelineBarrier2.
void mel_gpu__state_to_barrier2(Mel_Gpu_Resource_State state, bool is_depth, VkPipelineStageFlags2* stage, VkAccessFlags2* access, VkImageLayout* layout);

bool mel_gpu__texture_get(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex, Mel_Gpu_Texture_Obj** out);
bool mel_gpu__texture_view_get(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View view, Mel_Gpu_Texture_View_Obj** out);

Mel_SlotMap_Handle mel_gpu__table_insert(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, const void* obj);
void*              mel_gpu__table_get(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h);
bool               mel_gpu__table_remove(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h);
// U14: roll the handle generation now (use-after-free stays loud) but hold the slot index; reclaim it via a
// deferred-free entry once the destroyed resource's last submission retires (gpu-rhi.md §3.3).
bool               mel_gpu__table_remove_deferred(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h);
void               mel_gpu__table_reclaim(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, u32 index);

VkRenderPass mel_gpu__make_render_pass(Mel_Gpu_Device* dev, VkFormat color);
bool         mel_gpu__shader_modules(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh, VkShaderModule* vs, VkShaderModule* fs, const char** vs_entry, const char** fs_entry);
bool         mel_gpu__shader_compute_module(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh, VkShaderModule* cs, const char** cs_entry);
bool         mel_gpu__shader_reflection(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh, Mel_Gpu_Spirv_Reflection* out);

// U12 reflection: parse a SPIR-V blob for push-constant size, set-0 descriptor bounds, vertex input, and
// specialization constants (gpu-rhi.md §6.4). Backend-agnostic over SPIR-V; lives here because SPIR-V is
// the Vulkan blob form. Accumulates into `accum` so the vs+fs union is one struct; pass `vertex_stage` true
// only for the vertex blob (input variables of other stages are interpolants, not vertex attributes). The
// caller zeroes `accum` and sets `accum->alloc` before the first call; mel_gpu__reflection_free releases it.
void mel_gpu__spirv_reflect(const u32* code, usize size_bytes, bool vertex_stage, const Mel_Alloc* alloc, Mel_Gpu_Spirv_Reflection* accum);
void mel_gpu__reflection_free(Mel_Gpu_Spirv_Reflection* r);
bool         mel_gpu__pipeline_get(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe, VkPipeline* out_pipe, VkPipelineLayout* out_layout);
Mel_Gpu_Pipeline_Obj* mel_gpu__pipeline_obj(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe);
bool         mel_gpu__buffer_get(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf, VkBuffer* out);
bool         mel_gpu__sampler_get(Mel_Gpu_Device* dev, Mel_Gpu_Sampler sampler, VkSampler* out);
bool         mel_gpu__sampler_retain(Mel_Gpu_Device* dev, Mel_Gpu_Sampler sampler);

// U14 bindless heap (gpu-rhi.md §6.7). Created at device-create when the descriptor-indexing floor is
// granted and requested; registration writes one descriptor at the resource's handle index.
void mel_gpu__bindless_init(Mel_Gpu_Device* dev, bool want);
void mel_gpu__bindless_shutdown(Mel_Gpu_Device* dev);
void mel_gpu__bindless_register_sampled_image(Mel_Gpu_Device* dev, u32 slot, VkImageView view);
void mel_gpu__bindless_register_storage_image(Mel_Gpu_Device* dev, u32 slot, VkImageView view);
void mel_gpu__bindless_register_storage_buffer(Mel_Gpu_Device* dev, u32 slot, VkBuffer buf, VkDeviceSize range);
void mel_gpu__bindless_register_uniform_buffer(Mel_Gpu_Device* dev, u32 slot, VkBuffer buf, VkDeviceSize range);
void mel_gpu__bindless_register_sampler(Mel_Gpu_Device* dev, u32 slot, VkSampler sampler);

// U14 classic descriptor-set path (gpu-rhi.md §6.7). Allocate one set of `layout` from the device classic-
// pool chain (growing the chain on exhaustion), reporting which pool served it. The shutdown destroys the
// whole chain at device teardown. mel_gpu__bind_group_layout_vk resolves a layout handle's VkDescriptorSetLayout.
VkDescriptorSet mel_gpu__classic_descriptor_alloc(Mel_Gpu_Device* dev, VkDescriptorSetLayout layout, VkDescriptorPool* out_pool);
void            mel_gpu__classic_pools_shutdown(Mel_Gpu_Device* dev);
bool            mel_gpu__bind_group_layout_vk(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group_Layout layout, VkDescriptorSetLayout* out);
bool            mel_gpu__bind_group_set(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group, VkDescriptorSet* out);

void mel_gpu__allocator_init(Mel_Gpu_Device* dev);
void mel_gpu__allocator_shutdown(Mel_Gpu_Device* dev);
bool mel_gpu__mem_alloc(Mel_Gpu_Device* dev, VkMemoryRequirements req, VkMemoryPropertyFlags props, bool force_dedicated, Mel_Gpu_Allocation* out);
void mel_gpu__mem_free(Mel_Gpu_Device* dev, Mel_Gpu_Allocation* a);
