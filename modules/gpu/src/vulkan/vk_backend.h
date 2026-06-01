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

typedef struct
{
    Mel_Gpu_Resource_Header header;
    VkShaderModule          vs;
    VkShaderModule          fs;
    char*                   vs_entry;
    char*                   fs_entry;
} Mel_Gpu_Shader_Obj;

typedef struct
{
    Mel_Gpu_Resource_Header header;
    VkPipeline              pipeline;
    VkPipelineLayout        layout;
} Mel_Gpu_Pipeline_Obj;

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
    u64                marker;
    VkImage            image;
    VkImageView        view;
    VkBuffer           buffer;
    VkPipeline         pipeline;
    VkPipelineLayout   pipeline_layout;
    VkShaderModule     shader_vs;
    VkShaderModule     shader_fs;
    Mel_Gpu_Allocation alloc;
    bool               has_alloc;
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

    Mel_Mutex              obj_lock;
    Mel_Gpu_Resource_Table buffers;
    Mel_Gpu_Resource_Table textures;
    Mel_Gpu_Resource_Table texture_views;
    Mel_Gpu_Resource_Table shaders;
    Mel_Gpu_Resource_Table pipelines;
    Mel_Gpu_Resource_Table syncs;

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

VkFormat       mel_gpu__vk_format(Mel_Gpu_Format fmt);
Mel_Gpu_Format mel_gpu__vk_format_to_mel(VkFormat fmt);
VkImageAspectFlags mel_gpu__aspect_flags(Mel_Gpu_Texture_Aspect aspect, VkFormat fmt);

u32 mel_gpu__vk_find_memory_type(Mel_Gpu_Device* dev, u32 type_bits, VkMemoryPropertyFlags props);

VkCommandPool mel_gpu__thread_pool(Mel_Gpu_Device* dev, u32 family);

// U3 future-gated retirement. submit_serial_next reserves the id of a submission about to be made;
// submit_complete advances the watermark and frees every deferred resource gated at or below it.
u64  mel_gpu__submit_serial_next(Mel_Gpu_Device* dev);
void mel_gpu__submit_complete(Mel_Gpu_Device* dev, u64 serial);
void mel_gpu__defer_free(Mel_Gpu_Device* dev, Mel_Gpu_Deferred_Free entry);

// U17: maps a Mel_Gpu_Resource_State to the Vulkan (stage, access, layout) triple for legacy barriers.
void mel_gpu__state_to_barrier(Mel_Gpu_Resource_State state, bool is_depth, VkPipelineStageFlags* stage, VkAccessFlags* access, VkImageLayout* layout);

bool mel_gpu__texture_get(Mel_Gpu_Device* dev, Mel_Gpu_Texture tex, Mel_Gpu_Texture_Obj** out);
bool mel_gpu__texture_view_get(Mel_Gpu_Device* dev, Mel_Gpu_Texture_View view, Mel_Gpu_Texture_View_Obj** out);

Mel_SlotMap_Handle mel_gpu__table_insert(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, const void* obj);
void*              mel_gpu__table_get(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h);
bool               mel_gpu__table_remove(Mel_Gpu_Device* dev, Mel_Gpu_Resource_Table* t, Mel_SlotMap_Handle h);

VkRenderPass mel_gpu__make_render_pass(Mel_Gpu_Device* dev, VkFormat color);
bool         mel_gpu__shader_modules(Mel_Gpu_Device* dev, Mel_Gpu_Shader sh, VkShaderModule* vs, VkShaderModule* fs, const char** vs_entry, const char** fs_entry);
bool         mel_gpu__pipeline_get(Mel_Gpu_Device* dev, Mel_Gpu_Pipeline pipe, VkPipeline* out_pipe, VkPipelineLayout* out_layout);
bool         mel_gpu__buffer_get(Mel_Gpu_Device* dev, Mel_Gpu_Buffer buf, VkBuffer* out);

void mel_gpu__allocator_init(Mel_Gpu_Device* dev);
void mel_gpu__allocator_shutdown(Mel_Gpu_Device* dev);
bool mel_gpu__mem_alloc(Mel_Gpu_Device* dev, VkMemoryRequirements req, VkMemoryPropertyFlags props, bool force_dedicated, Mel_Gpu_Allocation* out);
void mel_gpu__mem_free(Mel_Gpu_Device* dev, Mel_Gpu_Allocation* a);
