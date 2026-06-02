#include "build.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// A persistent flag string (mel_* stores the pointer, not a copy). Configure runs once, so the leak is moot.
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

    // macOS: MoltenVK via the Homebrew loader (libvulkan) + Metal surface (Objective-C).
    mel_sources(lib, WHEN(.gpu = "vulkan", .platforms = MEL_ON(MACOS)), "src/vulkan/macos/*.m");
    mel_link(lib, MEL_PUBLIC, WHEN(.gpu = "vulkan", .platforms = MEL_ON(MACOS)), "-lvulkan");
    mel_cflags(lib, MEL_PUBLIC, WHEN(.gpu = "vulkan", .platforms = MEL_ON(MACOS)), "-I/opt/homebrew/include");
    mel_link(lib, MEL_PUBLIC, WHEN(.gpu = "vulkan", .platforms = MEL_ON(MACOS)), "-L/opt/homebrew/lib");
    mel_link(lib, MEL_PUBLIC, WHEN(.gpu = "vulkan", .platforms = MEL_ON(MACOS)), "-framework", "QuartzCore", "-framework", "Foundation");

    // win32: native build (clang/MSVC ABI) against the Vulkan SDK loader (vulkan-1) + Win32 surface (gpu-rhi.md §7.4).
    // The SDK include/lib live under %VULKAN_SDK%; vcvars does not add them, so inject them at configure time.
    mel_sources(lib, WHEN(.gpu = "vulkan", .platforms = MEL_ON(WIN32)), "src/vulkan/windows/*.c");
    mel_link(lib, MEL_PUBLIC, WHEN(.gpu = "vulkan", .platforms = MEL_ON(WIN32)), "-lvulkan-1");
    const char* vksdk = getenv("VULKAN_SDK");
    if (vksdk)
    {
        mel_cflags(lib, MEL_PUBLIC, WHEN(.gpu = "vulkan", .platforms = MEL_ON(WIN32)), mel_gpu__flag("-I", vksdk, "/Include"));
        mel_link(lib, MEL_PUBLIC, WHEN(.gpu = "vulkan", .platforms = MEL_ON(WIN32)), mel_gpu__flag("-L", vksdk, "/Lib"));
    }

    // D3D12: native win32 (clang/MSVC ABI) against the in-box Windows SDK (d3d12.h / dxgi1_6.h, which vcvars
    // already puts on INCLUDE/LIB — no SDK-path injection). dxguid.lib supplies the IID_* GUID symbols.
    // gpu-rhi.md §12 M2 co-primary; design/gpu-d3d12.md for phasing. The Agility SDK ceiling rides later.
    mel_sources(lib, WHEN(.gpu = "d3d12", .platforms = MEL_ON(WIN32)), "src/d3d12/*.c");
    mel_defines(lib, MEL_PRIVATE, WHEN(.gpu = "d3d12"), "MEL_GPU_D3D12=1");
    mel_link(lib, MEL_PUBLIC, WHEN(.gpu = "d3d12", .platforms = MEL_ON(WIN32)), "-ld3d12", "-ldxgi", "-ldxguid");

    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "async");
    mel_depends(lib, "reactor");
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

    // Visual/golden tests (--gpu=vulkan). Same scaffold as gpu-vulkan: each technique renders offscreen, reads
    // back, pixel-asserts, AND dumps a viewable PPM. The body is #if MEL_GPU_VULKAN-guarded (skips otherwise).
    Mel_Target* vistest = mel_add_test(b, "gpu-visual");
    mel_sources(vistest, ALWAYS, "test/test_visual.c");
    mel_sources(vistest, ALWAYS, "../../tools/test/src/runner.c");
    mel_defines(vistest, MEL_PRIVATE, WHEN(.gpu = "vulkan"), "MEL_GPU_VULKAN=1");
    mel_link(vistest, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "AppKit");
    mel_depends(vistest, "test");
    mel_depends(vistest, "gpu");
    mel_depends(vistest, "core");
    mel_depends(vistest, "allocator");
    mel_depends(vistest, "collection");
    mel_depends(vistest, "reactor");

    // D3D12 backend tests (win32, --gpu=d3d12). The test body is #if MEL_GPU_D3D12-guarded, so the target
    // links to an empty 0-test runner on any non-d3d12 build and is meaningful only on win-pilot.
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
