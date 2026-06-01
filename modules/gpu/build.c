#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "gpu");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");

    mel_sources(lib, WHEN(.gpu = "vulkan"), "src/vulkan/*.c");
    mel_defines(lib, MEL_PRIVATE, WHEN(.gpu = "vulkan"), "MEL_GPU_VULKAN=1");
    mel_sources(lib, WHEN(.gpu = "vulkan", .platforms = MEL_ON(MACOS)), "src/vulkan/macos/*.m");
    mel_link(lib, MEL_PUBLIC, WHEN(.gpu = "vulkan"), "-lvulkan");
    mel_cflags(lib, MEL_PUBLIC, WHEN(.gpu = "vulkan", .platforms = MEL_ON(MACOS)), "-I/opt/homebrew/include");
    mel_link(lib, MEL_PUBLIC, WHEN(.gpu = "vulkan", .platforms = MEL_ON(MACOS)), "-L/opt/homebrew/lib");
    mel_link(lib, MEL_PUBLIC, WHEN(.gpu = "vulkan", .platforms = MEL_ON(MACOS)), "-framework", "QuartzCore", "-framework", "Foundation");

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
}
