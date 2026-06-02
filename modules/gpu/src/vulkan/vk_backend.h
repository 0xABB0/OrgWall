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
#include <gpu/query.h>

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

typedef struct
{
    u8  min_filter, mag_filter, mip_filter, wrap_u, wrap_v, wrap_w, compare, border;
    f32 max_anisotropy, lod_min, lod_max;
} Mel_Gpu_Sampler_Key;

typedef struct
{
    Mel_Gpu_Resource_Header header;
    VkSampler               sampler;
    Mel_Gpu_Sampler_Key     key;
    u64                     hash;
    u32                     refcount;
} Mel_Gpu_Sampler_Obj;

typedef struct
{
    u32  binding;
    u32  array_len;
    bool runtime_array;
} Mel_Gpu_Reflect_Set0_Binding;

typedef struct
{
    u32            location;
    Mel_Gpu_Format format;
    u32            offset;
} Mel_Gpu_Reflect_Vertex_Attr;

typedef struct
{
    u32 id;
    u32 bytes;
} Mel_Gpu_Reflect_Spec_Constant;

typedef struct
{
    u32  push_constant_size;
    bool uses_bindless_set;

    Mel_Gpu_Reflect_Set0_Binding* set0;
    u32                           set0_count;

    Mel_Gpu_Reflect_Vertex_Attr* vertex_attrs;
    u32                          vertex_attr_count;
    u32                          vertex_stride;

    Mel_Gpu_Reflect_Spec_Constant* spec_constants;
    u32                            spec_constant_count;

    const Mel_Alloc* alloc;
} Mel_Gpu_Spirv_Reflection;

typedef struct
{
    Mel_Gpu_Resource_Header  header;
    VkShaderModule           vs;
    VkShaderModule           fs;
    VkShaderModule           cs;
    char*                    vs_entry;
    char*                    fs_entry;
    char*                    cs_entry;
    Mel_Gpu_Spirv_Reflection reflection;
} Mel_Gpu_Shader_Obj;

typedef struct
{
    Mel_Gpu_Resource_Header header;
    VkPipeline              pipeline;
    VkPipelineLayout        layout;
    VkDescriptorSetLayout   static_sampler_layout;
    bool                    bindless;
    VkPipelineBindPoint     bind_point;
    VkShaderStageFlags      pc_stages;
    Mel_Gpu_Sampler*        static_samplers;
    u32                     static_sampler_count;
} Mel_Gpu_Pipeline_Obj;

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
    VkDescriptorPool        pool;
    Mel_SlotMap_Handle      layout;
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
    Mel_Gpu_Resource_Header header;
    VkQueryPool             pool;
    Mel_Gpu_Query_Type      type;
    u32                     count;
    f64                     period_ns;
} Mel_Gpu_Query_Pool_Obj;

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
    VkDescriptorSet       descriptor_set;
    VkDescriptorPool      descriptor_set_pool;
    Mel_Gpu_Allocation    alloc;
    bool                  has_alloc;
    Mel_Gpu_Resource_Table* reclaim_table;
    u32                     reclaim_index;
    bool                    has_reclaim;
} Mel_Gpu_Deferred_Free;

typedef struct
{
    u32                    tex_index;
    u32                    tex_generation;
    u32                    mip;
    u32                    layer;
    Mel_Gpu_Resource_State state;
} Mel_Gpu_Cmd_State_Entry;

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
    Mel_Mutex             lock;
} Mel_Gpu_Bindless;

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
    f32                              max_sampler_anisotropy;
    bool                             bda_enabled;

    bool               feat_fill_non_solid;
    bool               feat_depth_bounds;
    bool               feat_depth_bias_clamp;
    bool               feat_sample_rate_shading;
    VkSampleCountFlags fb_color_samples;
    VkSampleCountFlags fb_depth_samples;

    Mel_Mutex              obj_lock;
    Mel_Gpu_Resource_Table buffers;
    Mel_Gpu_Resource_Table textures;
    Mel_Gpu_Resource_Table texture_views;
    Mel_Gpu_Resource_Table samplers;
    Mel_Gpu_Resource_Table shaders;
    Mel_Gpu_Resource_Table pipelines;
    Mel_Gpu_Resource_Table syncs;
    Mel_Gpu_Resource_Table query_pools;
    Mel_Gpu_Resource_Table bind_group_layouts;
    Mel_Gpu_Resource_Table bind_groups;

    Mel_Mutex         classic_pool_lock;
    VkDescriptorPool* classic_pools;
    u32               classic_pool_count;
    u32               classic_pool_cap;

    Mel_Gpu_Bindless        bindless;
    Mel_Mutex               sampler_lock;
    Mel_Gpu_Sampler_Intern* sampler_interns;
    u32                     sampler_intern_count;
    u32                     sampler_intern_cap;

    Mel_Gpu_Pending_Submit* pending;
    u32                     pending_count;
    u32                     pending_cap;
    bool                    submit_poller_registered;

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
    VkPipelineBindPoint cur_bind_point;
    VkShaderStageFlags  cur_pc_stages;
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
VkSurfaceKHR mel_gpu__vk_create_win32_surface(VkInstance instance, void* hwnd);

VkFormat       mel_gpu__vk_format(Mel_Gpu_Format fmt);
Mel_Gpu_Format mel_gpu__vk_format_to_mel(VkFormat fmt);
VkImageAspectFlags mel_gpu__aspect_flags(Mel_Gpu_Texture_Aspect aspect, VkFormat fmt);
VkCompareOp mel_gpu__vk_compare_op(Mel_Gpu_Compare_Op c);

u32 mel_gpu__vk_find_memory_type(Mel_Gpu_Device* dev, u32 type_bits, VkMemoryPropertyFlags props);

VkCommandPool mel_gpu__thread_pool(Mel_Gpu_Device* dev, u32 family);

u64  mel_gpu__submit_serial_next(Mel_Gpu_Device* dev);
void mel_gpu__submit_complete(Mel_Gpu_Device* dev, u64 serial);
void mel_gpu__defer_free(Mel_Gpu_Device* dev, Mel_Gpu_Deferred_Free entry);

void mel_gpu__state_to_barrier(Mel_Gpu_Resource_State state, bool is_depth, VkPipelineStageFlags* stage, VkAccessFlags* access, VkImageLayout* layout);
void mel_gpu__state_to_barrier2(Mel_Gpu_Resource_State state, bool is_depth, VkPipelineStageFlags2* stage, VkAccessFlags2* access, VkImageLayout* layout);

bool mel_gpu__texture_get(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex, Mel_Gpu_Texture_Obj* out);
bool mel_gpu__texture_view_get(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View view, Mel_Gpu_Texture_View_Obj* out);

Mel_SlotMap_Handle mel_gpu__table_insert(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, const void* obj);
void*              mel_gpu__table_get(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h);
bool               mel_gpu__table_get_copy(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h, void* out);
bool               mel_gpu__table_alive(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h);
bool               mel_gpu__sampler_refcount_add(Mel_Gpu_Device* dev, Mel_SlotMap_Handle h, i32 delta, u32* out_after);
bool               mel_gpu__table_remove(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h);

void        mel_gpu__track_enter(Mel_Gpu_Device* dev, const void* object, Mel_Gpu_Concurrency cls);
void        mel_gpu__track_exit(Mel_Gpu_Device* dev, const void* object);
const void* mel_gpu__track_key(const Mel_Gpu_Resource_Table* t, u32 index);
bool               mel_gpu__table_remove_deferred(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h);
void               mel_gpu__table_reclaim(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, u32 index);

VkRenderPass mel_gpu__make_render_pass(Mel_Gpu_Device* dev, VkFormat color);
bool         mel_gpu__shader_modules(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh, VkShaderModule* vs, VkShaderModule* fs, const char** vs_entry, const char** fs_entry);
bool         mel_gpu__shader_compute_module(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh, VkShaderModule* cs, const char** cs_entry);
bool         mel_gpu__shader_reflection(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh, Mel_Gpu_Spirv_Reflection* out);

void mel_gpu__spirv_reflect(const u32* code, usize size_bytes, bool vertex_stage, const Mel_Alloc* alloc, Mel_Gpu_Spirv_Reflection* accum);
void mel_gpu__reflection_free(Mel_Gpu_Spirv_Reflection* r);
bool         mel_gpu__pipeline_get(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe, VkPipeline* out_pipe, VkPipelineLayout* out_layout);
bool         mel_gpu__pipeline_obj(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe, Mel_Gpu_Pipeline_Obj* out);
bool         mel_gpu__buffer_get(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf, VkBuffer* out);
bool         mel_gpu__sampler_get(Mel_Gpu_Device* dev, Mel_Gpu_Sampler sampler, VkSampler* out);
bool         mel_gpu__sampler_retain(Mel_Gpu_Device* dev, Mel_Gpu_Sampler sampler);

void mel_gpu__bindless_init(Mel_Gpu_Device* dev, bool want);
void mel_gpu__bindless_shutdown(Mel_Gpu_Device* dev);
u32  mel_gpu__heap_cap_for_class(Mel_Gpu_Device* dev, u32 binding_class);
bool mel_gpu__bindless_slot_fits(Mel_Gpu_Device* dev, u32 binding_class, u32 slot);
bool mel_gpu__bindless_register_sampled_image(Mel_Gpu_Device* dev, u32 slot, VkImageView view);
bool mel_gpu__bindless_register_storage_image(Mel_Gpu_Device* dev, u32 slot, VkImageView view);
bool mel_gpu__bindless_register_storage_buffer(Mel_Gpu_Device* dev, u32 slot, VkBuffer buf, VkDeviceSize range);
bool mel_gpu__bindless_register_uniform_buffer(Mel_Gpu_Device* dev, u32 slot, VkBuffer buf, VkDeviceSize range);
bool mel_gpu__bindless_register_sampler(Mel_Gpu_Device* dev, u32 slot, VkSampler sampler);

VkDescriptorSet mel_gpu__classic_descriptor_alloc(Mel_Gpu_Device* dev, VkDescriptorSetLayout layout, VkDescriptorPool* out_pool);
void            mel_gpu__classic_pools_shutdown(Mel_Gpu_Device* dev);
bool            mel_gpu__bind_group_layout_vk(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group_Layout layout, VkDescriptorSetLayout* out);
bool            mel_gpu__bind_group_set(Mel_Gpu_Device* dev, Mel_Gpu_Bind_Group group, VkDescriptorSet* out);

void mel_gpu__allocator_init(Mel_Gpu_Device* dev);
void mel_gpu__allocator_shutdown(Mel_Gpu_Device* dev);
bool mel_gpu__mem_alloc(Mel_Gpu_Device* dev, VkMemoryRequirements req, VkMemoryPropertyFlags props, bool force_dedicated, Mel_Gpu_Allocation* out);
void mel_gpu__mem_free(Mel_Gpu_Device* dev, Mel_Gpu_Allocation* a);
