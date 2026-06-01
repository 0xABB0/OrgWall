#pragma once

#include <vulkan/vulkan.h>

#include <core/types.h>
#include <allocator/allocator.h>
#include <collection.slotmap/slotmap.h>
#include <reactor/reactor.h>
#include <thread/mutex.h>
#include <debug/assert.h>

#include <gpu/handle.h>
#include <gpu/caps.h>
#include <gpu/device.h>
#include <gpu/future.h>
#include <gpu/threading.h>
#include <gpu/format.h>
#include <gpu/queue.h>
#include <gpu/buffer.h>
#include <gpu/shader.h>
#include <gpu/pipeline.h>
#include <gpu/sync.h>
#include <gpu/surface.h>
#include <gpu/swapchain.h>
#include <gpu/command.h>

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
    bool            active;
} Mel_Gpu_Pending_Submit;

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

    u32     graphics_family;
    VkQueue graphics_queue;
    Mel_Mutex submit_lock;

    VkPhysicalDeviceMemoryProperties mem_props;
    Mel_Gpu_Allocator*               allocator;

    Mel_Mutex              obj_lock;
    Mel_Gpu_Resource_Table buffers;
    Mel_Gpu_Resource_Table shaders;
    Mel_Gpu_Resource_Table pipelines;
    Mel_Gpu_Resource_Table syncs;

    Mel_Gpu_Pending_Submit* pending;
    u32                     pending_count;
    u32                     pending_cap;
    bool                    submit_poller_registered;
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

u32 mel_gpu__vk_find_memory_type(Mel_Gpu_Device* dev, u32 type_bits, VkMemoryPropertyFlags props);

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
