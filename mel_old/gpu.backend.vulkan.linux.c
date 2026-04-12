#include "core.platform.h"
#if MEL_PLATFORM_LINUX

#include "gpu.backend.vulkan.h"
#include <dlfcn.h>

PFN_vkGetInstanceProcAddr mel_gpu_vulkan_load(void)
{
    void* lib = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
    if (!lib) return nullptr;
    return (PFN_vkGetInstanceProcAddr)dlsym(lib, "vkGetInstanceProcAddr");
}

#endif
