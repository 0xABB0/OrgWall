#include "build.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* mel_gpu__flag(const char* prefix, const char* mid, const char* suffix)
{
    size_t n = strlen(prefix) + strlen(mid) + strlen(suffix) + 1;
    char*  s = malloc(n);
    snprintf(s, n, "%s%s%s", prefix, mid, suffix);
    return s;
}

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "gpu");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");

    mel_sources(lib, WHEN(.gpu = "vulkan"), "src/vulkan/*.c");
    mel_defines(lib, MEL_PRIVATE, WHEN(.gpu = "vulkan"), "MEL_GPU_VULKAN=1");

    mel_sources(lib, WHEN(.gpu = "vulkan", .platforms = MEL_ON(MACOS)), "src/vulkan/macos/*.m");
    mel_link(lib, MEL_PUBLIC, WHEN(.gpu = "vulkan", .platforms = MEL_ON(MACOS)), "-lvulkan");
    mel_cflags(lib, MEL_PUBLIC, WHEN(.gpu = "vulkan", .platforms = MEL_ON(MACOS)), "-I/opt/homebrew/include");
    mel_link(lib, MEL_PUBLIC, WHEN(.gpu = "vulkan", .platforms = MEL_ON(MACOS)), "-L/opt/homebrew/lib");
    mel_link(lib, MEL_PUBLIC, WHEN(.gpu = "vulkan", .platforms = MEL_ON(MACOS)), "-framework", "QuartzCore", "-framework", "Foundation");

    mel_sources(lib, WHEN(.gpu = "vulkan", .platforms = MEL_ON(WIN32)), "src/vulkan/windows/*.c");
    mel_link(lib, MEL_PUBLIC, WHEN(.gpu = "vulkan", .platforms = MEL_ON(WIN32)), "-lvulkan-1");
    const char* vksdk = getenv("VULKAN_SDK");
    if (vksdk)
    {
        mel_cflags(lib, MEL_PUBLIC, WHEN(.gpu = "vulkan", .platforms = MEL_ON(WIN32)), mel_gpu__flag("-I", vksdk, "/Include"));
        mel_link(lib, MEL_PUBLIC, WHEN(.gpu = "vulkan", .platforms = MEL_ON(WIN32)), mel_gpu__flag("-L", vksdk, "/Lib"));
    }

    mel_sources(lib, WHEN(.gpu = "d3d12", .platforms = MEL_ON(WIN32)), "src/d3d12/*.c");
    mel_defines(lib, MEL_PRIVATE, WHEN(.gpu = "d3d12"), "MEL_GPU_D3D12=1");
    mel_link(lib, MEL_PUBLIC, WHEN(.gpu = "d3d12", .platforms = MEL_ON(WIN32)), "-ld3d12", "-ldxgi", "-ldxguid");

    mel_sources(lib, WHEN(.gpu = "metal", .platforms = MEL_ON(MACOS)), "src/metal/macos/*.m");
    mel_defines(lib, MEL_PRIVATE, WHEN(.gpu = "metal"), "MEL_GPU_METAL=1");
    mel_link(lib, MEL_PUBLIC, WHEN(.gpu = "metal", .platforms = MEL_ON(MACOS)), "-framework", "Metal", "-framework", "QuartzCore", "-framework", "Foundation", "-framework", "AppKit");

    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "reactor");
    mel_depends(lib, "executor");
    mel_depends(lib, "future");
    mel_depends(lib, "log");
    mel_depends(lib, "debug");
    mel_depends(lib, "string");
    mel_depends(lib, "thread");
    mel_depends(lib, "time");
    mel_depends(lib, "thermal");
    mel_depends(lib, "power");

    Mel_Target* found = mel_add_test(b, "gpu-foundation");
    mel_sources(found, ALWAYS, "test/test_foundation.c");
    mel_sources(found, ALWAYS, "../../tools/test/src/runner.c");
    mel_link(found, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "AppKit");
    mel_depends(found, "test");
    mel_depends(found, "gpu");
    mel_depends(found, "core");
    mel_depends(found, "allocator");
    mel_depends(found, "collection");
    mel_depends(found, "reactor");

    Mel_Target* restest = mel_add_test(b, "gpu-resources");
    mel_sources(restest, ALWAYS, "test/test_resources.c");
    mel_sources(restest, ALWAYS, "../../tools/test/src/runner.c");
    mel_defines(restest, MEL_PRIVATE, WHEN(.gpu = "vulkan"), "MEL_GPU_VULKAN=1");
    mel_defines(restest, MEL_PRIVATE, WHEN(.gpu = "metal"), "MEL_GPU_METAL=1");
    mel_link(restest, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "AppKit");
    mel_depends(restest, "test");
    mel_depends(restest, "gpu");
    mel_depends(restest, "core");
    mel_depends(restest, "allocator");
    mel_depends(restest, "collection");
    mel_depends(restest, "reactor");

    Mel_Target* vktest = mel_add_test(b, "gpu-vulkan");
    mel_sources(vktest, ALWAYS, "test/test_vulkan.c");
    mel_sources(vktest, ALWAYS, "../../tools/test/src/runner.c");
    mel_defines(vktest, MEL_PRIVATE, WHEN(.gpu = "vulkan"), "MEL_GPU_VULKAN=1");
    mel_link(vktest, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "AppKit");
    mel_depends(vktest, "test");
    mel_depends(vktest, "gpu");
    mel_depends(vktest, "core");
    mel_depends(vktest, "allocator");
    mel_depends(vktest, "collection");
    mel_depends(vktest, "reactor");

    Mel_Target* stress = mel_add_test(b, "gpu-stress");
    mel_sources(stress, ALWAYS, "test/test_stress.c");
    mel_sources(stress, ALWAYS, "../../tools/test/src/runner.c");
    mel_defines(stress, MEL_PRIVATE, WHEN(.gpu = "vulkan"), "MEL_GPU_VULKAN=1");
    mel_link(stress, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "AppKit");
    mel_depends(stress, "test");
    mel_depends(stress, "gpu");
    mel_depends(stress, "core");
    mel_depends(stress, "allocator");
    mel_depends(stress, "collection");
    mel_depends(stress, "reactor");
    Mel_Target* conc = mel_add_test(b, "gpu-concurrency");
    mel_sources(conc, ALWAYS, "test/test_concurrency.c");
    mel_sources(conc, ALWAYS, "../../tools/test/src/runner.c");
    mel_defines(conc, MEL_PRIVATE, WHEN(.gpu = "vulkan"), "MEL_GPU_VULKAN=1");
    mel_link(conc, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "AppKit");
    mel_depends(conc, "test");
    mel_depends(conc, "gpu");
    mel_depends(conc, "core");
    mel_depends(conc, "allocator");
    mel_depends(conc, "collection");
    mel_depends(conc, "reactor");

    Mel_Target* vistest = mel_add_test(b, "gpu-visual");
    mel_sources(vistest, ALWAYS, "test/test_visual.c");
    mel_sources(vistest, ALWAYS, "test/img_golden.c");
    mel_sources(vistest, ALWAYS, "../../tools/test/src/runner.c");
    mel_defines(vistest, MEL_PRIVATE, WHEN(.gpu = "vulkan"), "MEL_GPU_VULKAN=1");
    mel_link(vistest, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "AppKit");
    mel_depends(vistest, "test");
    mel_depends(vistest, "gpu");
    mel_depends(vistest, "core");
    mel_depends(vistest, "allocator");
    mel_depends(vistest, "collection");
    mel_depends(vistest, "reactor");

    Mel_Target* bench = mel_add_test(b, "gpu-bench");
    mel_sources(bench, ALWAYS, "test/test_bench.c");
    mel_sources(bench, ALWAYS, "../../tools/test/src/runner.c");
    mel_defines(bench, MEL_PRIVATE, WHEN(.gpu = "vulkan"), "MEL_GPU_VULKAN=1");
    mel_link(bench, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "AppKit");
    mel_depends(bench, "test");
    mel_depends(bench, "gpu");
    mel_depends(bench, "core");
    mel_depends(bench, "allocator");
    mel_depends(bench, "collection");
    mel_depends(bench, "reactor");
    mel_depends(bench, "time");

    Mel_Target* d3dtest = mel_add_test(b, "gpu-d3d12");
    mel_sources(d3dtest, ALWAYS, "test/test_d3d12.c");
    mel_sources(d3dtest, ALWAYS, "../../tools/test/src/runner.c");
    mel_defines(d3dtest, MEL_PRIVATE, WHEN(.gpu = "d3d12"), "MEL_GPU_D3D12=1");
    mel_depends(d3dtest, "test");
    mel_depends(d3dtest, "gpu");
    mel_depends(d3dtest, "core");
    mel_depends(d3dtest, "allocator");
    mel_depends(d3dtest, "collection");
    mel_depends(d3dtest, "reactor");
}
