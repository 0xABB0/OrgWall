#ifndef MEL_VK_SWAPCHAIN_H
#define MEL_VK_SWAPCHAIN_H

#include "vk_context.h"

#define MEL_MAX_FRAMES_IN_FLIGHT 3

typedef struct
{
    VkSwapchainKHR swapchain;
    VkFormat format;
    VkExtent2D extent;

    u32 image_count;
    VkImage* images;
    VkImageView* image_views;

    VkCommandPool command_pools[MEL_MAX_FRAMES_IN_FLIGHT];
    VkCommandBuffer command_buffers[MEL_MAX_FRAMES_IN_FLIGHT];

    VkSemaphore image_available[MEL_MAX_FRAMES_IN_FLIGHT];
    VkSemaphore render_finished[MEL_MAX_FRAMES_IN_FLIGHT];
    VkFence in_flight[MEL_MAX_FRAMES_IN_FLIGHT];

    u32 current_frame;
    u32 current_image;
} Mel_VkSwapchain;

bool mel_vk_swapchain_init(Mel_VkSwapchain* sc, Mel_VkContext* ctx, u32 width, u32 height);
void mel_vk_swapchain_shutdown(Mel_VkSwapchain* sc, Mel_VkContext* ctx);

bool mel_vk_swapchain_recreate(Mel_VkSwapchain* sc, Mel_VkContext* ctx, u32 width, u32 height);

bool mel_vk_swapchain_acquire(Mel_VkSwapchain* sc, Mel_VkContext* ctx);
bool mel_vk_swapchain_present(Mel_VkSwapchain* sc, Mel_VkContext* ctx);

[[nodiscard]] VkSemaphore mel_vk_swapchain_image_available(Mel_VkSwapchain* sc);
[[nodiscard]] VkSemaphore mel_vk_swapchain_render_finished(Mel_VkSwapchain* sc);
[[nodiscard]] VkFence mel_vk_swapchain_in_flight_fence(Mel_VkSwapchain* sc);
[[nodiscard]] VkCommandBuffer mel_vk_swapchain_command_buffer(Mel_VkSwapchain* sc);

void mel_vk_swapchain_begin_frame(Mel_VkSwapchain* sc, Mel_VkContext* ctx);
void mel_vk_swapchain_end_frame(Mel_VkSwapchain* sc, Mel_VkContext* ctx);

void mel_vk_swapchain_clear(Mel_VkSwapchain* sc, Mel_VkContext* ctx, f32 r, f32 g, f32 b, f32 a);

void mel_vk_swapchain_begin_rendering(Mel_VkSwapchain* sc, Mel_VkContext* ctx, f32 r, f32 g, f32 b, f32 a);
void mel_vk_swapchain_end_rendering(Mel_VkSwapchain* sc, Mel_VkContext* ctx);

#endif
