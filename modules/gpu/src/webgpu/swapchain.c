#include "webgpu_backend.h"

static void configure(Mel_Gpu_Swapchain* sc)
{
    mel_gpu_surface_reconfigure(sc->surface, sc->width, sc->height);

    WGPUSurfaceConfiguration cfg = {
        .device = sc->device->device,
        .format = sc->format,
        .usage = WGPUTextureUsage_RenderAttachment,
        .width = (u32)sc->width,
        .height = (u32)sc->height,
        .alphaMode = WGPUCompositeAlphaMode_Auto,
        .presentMode = WGPUPresentMode_Fifo,
    };
    wgpuSurfaceConfigure(sc->surface->surface, &cfg);
}

Mel_Gpu_Swapchain* mel_gpu_swapchain_create_opt(Mel_Gpu_Device* dev, Mel_Gpu_Swapchain_Opt opt)
{
    if (!dev || !opt.surface || !opt.surface->surface)
        return NULL;

    Mel_Gpu_Swapchain* sc = calloc(1, sizeof *sc);
    if (!sc)
        return NULL;
    sc->device = dev;
    sc->surface = opt.surface;
    sc->mel_format = opt.format == MEL_GPU_FORMAT_UNDEFINED ? MEL_GPU_FORMAT_BGRA8_UNORM : opt.format;
    sc->format = mel_gpu__wgpu_color_format(sc->mel_format);
    sc->width = opt.width > 0 ? opt.width : 1;
    sc->height = opt.height > 0 ? opt.height : 1;
    sc->cmd.swapchain = sc;

#if !defined(__EMSCRIPTEN__)
    // The hardcoded BGRA8 default is right for Metal but unrepresentable in
    // Android's gralloc, which has no BGRA AHardwareBuffer mapping. Adopt a
    // format the surface actually advertises, preferring a plain (non-sRGB)
    // RGBA8/BGRA8 so the shader's linear output lands correctly.
    if (dev->adapter)
    {
        WGPUSurfaceCapabilities caps = { 0 };
        if (wgpuSurfaceGetCapabilities(sc->surface->surface, dev->adapter, &caps) == WGPUStatus_Success && caps.formatCount > 0)
        {
            WGPUTextureFormat chosen = caps.formats[0];
            for (size_t i = 0; i < caps.formatCount; i++)
            {
                if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm || caps.formats[i] == WGPUTextureFormat_RGBA8Unorm)
                {
                    chosen = caps.formats[i];
                    break;
                }
            }
            sc->format = chosen;
            Mel_Gpu_Format m = mel_gpu__mel_color_format(chosen);
            if (m != MEL_GPU_FORMAT_UNDEFINED)
                sc->mel_format = m;
        }
        wgpuSurfaceCapabilitiesFreeMembers(caps);
    }
#endif

    configure(sc);
    return sc;
}

void mel_gpu_swapchain_destroy(Mel_Gpu_Swapchain* sc)
{
    if (!sc)
        return;
    if (sc->cur_view)
        wgpuTextureViewRelease(sc->cur_view);
    if (sc->cur_texture)
        wgpuTextureRelease(sc->cur_texture);
    free(sc);
}

void mel_gpu_swapchain_resize(Mel_Gpu_Swapchain* sc, i32 width, i32 height)
{
    if (!sc || width <= 0 || height <= 0)
        return;
    sc->width = width;
    sc->height = height;
    configure(sc);
}

Mel_Gpu_Format mel_gpu_swapchain_format(const Mel_Gpu_Swapchain* sc) { return sc ? sc->mel_format : MEL_GPU_FORMAT_UNDEFINED; }
