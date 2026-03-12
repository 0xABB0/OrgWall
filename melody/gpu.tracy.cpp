#include "gpu.tracy.h"
#include "gpu.device.h"
#include "string.str8.h"

#ifdef new
#undef new
#endif

#include <tracy/TracyVulkan.hpp>

#include <SDL3/SDL.h>

struct Mel_Gpu_Tracy_Ctx {
    TracyVkCtx ctx;
    bool enabled;
};

struct Mel_Gpu_Tracy_Zone_State {
    tracy::VkCtxScope* scope;
};

extern "C" bool mel_gpu_tracy_init(Mel_Gpu_Tracy_Ctx** out_ctx, Mel_Gpu_Device* dev, VkQueue queue, VkCommandBuffer cmd, str8 name)
{
    assert(out_ctx != nullptr);
    assert(dev != nullptr);

    if (dev->has_portability_subset)
    {
        SDL_Log("Tracy GPU disabled: Vulkan portability subset drivers reject Tracy's default timestamp query pool size");
        Mel_Gpu_Tracy_Ctx* tracy = new Mel_Gpu_Tracy_Ctx{};
        tracy->ctx = nullptr;
        tracy->enabled = false;
        *out_ctx = tracy;
        return true;
    }

    Mel_Gpu_Tracy_Ctx* tracy = new Mel_Gpu_Tracy_Ctx{};
    tracy->enabled = true;
    tracy->ctx = TracyVkContextCalibrated(
        dev->physical_device,
        dev->device,
        queue,
        cmd,
        vkGetPhysicalDeviceCalibrateableTimeDomainsEXT,
        vkGetCalibratedTimestampsEXT);

    if (tracy->ctx && !str8_is_empty(name))
        TracyVkContextName(tracy->ctx, (const char*)name.data, (uint16_t)name.len);

    *out_ctx = tracy;
    return true;
}

extern "C" void mel_gpu_tracy_shutdown(Mel_Gpu_Tracy_Ctx* ctx)
{
    if (!ctx) return;
    if (ctx->ctx)
        TracyVkDestroy(ctx->ctx);
    delete ctx;
}

extern "C" void mel_gpu_tracy_collect(Mel_Gpu_Tracy_Ctx* ctx, VkCommandBuffer cmd)
{
    if (!ctx || !ctx->enabled || !ctx->ctx || cmd == VK_NULL_HANDLE) return;
    TracyVkCollect(ctx->ctx, cmd);
}

extern "C" Mel_Gpu_Tracy_Zone mel_gpu_tracy_zone_begin(Mel_Gpu_Tracy_Ctx* ctx, VkCommandBuffer cmd, str8 name)
{
    if (!ctx || !ctx->enabled || !ctx->ctx || cmd == VK_NULL_HANDLE || str8_is_empty(name))
        return (Mel_Gpu_Tracy_Zone){0};

    Mel_Gpu_Tracy_Zone_State* state = new Mel_Gpu_Tracy_Zone_State;
    state->scope = new tracy::VkCtxScope(
        ctx->ctx,
        __LINE__,
        __FILE__,
        sizeof(__FILE__) - 1,
        __FUNCTION__,
        sizeof(__FUNCTION__) - 1,
        (const char*)name.data,
        (size_t)name.len,
        cmd,
        true);
    return (Mel_Gpu_Tracy_Zone){ .state = state };
}

extern "C" void mel_gpu_tracy_zone_end(Mel_Gpu_Tracy_Zone zone)
{
    Mel_Gpu_Tracy_Zone_State* state = (Mel_Gpu_Tracy_Zone_State*)zone.state;
    if (!state) return;
    delete state->scope;
    delete state;
}
