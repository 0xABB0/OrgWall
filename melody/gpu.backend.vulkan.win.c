#include "core.platform.h"
#if MEL_PLATFORM_WINDOWS

#include "gpu.backend.vulkan.h"
#include <windows.h>

PFN_vkGetInstanceProcAddr mel_gpu_vulkan_load(void)
{
    HMODULE lib = LoadLibraryA("vulkan-1.dll");
    if (!lib) return nullptr;
    return (PFN_vkGetInstanceProcAddr)GetProcAddress(lib, "vkGetInstanceProcAddr");
}

#endif
