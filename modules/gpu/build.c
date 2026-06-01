#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "gpu");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");

    mel_sources(lib, WHEN(.gpu = "metal"), "src/metal/*.m");
    mel_link(lib, MEL_PUBLIC, WHEN(.gpu = "metal"), "-framework", "Metal", "-framework", "QuartzCore", "-framework", "Foundation");

    mel_sources(lib, WHEN(.gpu = "vulkan"), "src/vulkan/*.c");
    mel_exclude_source(lib, WHEN(.gpu = "vulkan", .platforms = MEL_ON(MACOS) | MEL_ON(LINUX)), "src/vulkan/android_surface.c");
    mel_sources(lib, WHEN(.gpu = "vulkan", .platforms = MEL_ON(MACOS)), "src/vulkan/surface_apple.m");
    mel_link(lib, MEL_PUBLIC, WHEN(.gpu = "vulkan"), "-lvulkan");
    mel_cflags(lib, MEL_PUBLIC, WHEN(.gpu = "vulkan", .platforms = MEL_ON(MACOS)), "-I/opt/homebrew/include");
    mel_link(lib, MEL_PUBLIC, WHEN(.gpu = "vulkan", .platforms = MEL_ON(MACOS)), "-L/opt/homebrew/lib");

    mel_sources(lib, WHEN(.gpu = "webgpu"), "src/webgpu/*.c");
    mel_exclude_source(lib, WHEN(.gpu = "webgpu", .platforms = MEL_ON(MACOS) | MEL_ON(WASM)), "src/webgpu/surface_android.c");
    mel_exclude_source(lib, WHEN(.gpu = "webgpu", .platforms = MEL_ON(MACOS) | MEL_ON(ANDROID)), "src/webgpu/surface_web.c");
    mel_sources(lib, WHEN(.gpu = "webgpu", .platforms = MEL_ON(MACOS)), "src/webgpu/surface_cocoa.m");
    mel_cflags(lib, MEL_PUBLIC, WHEN(.runtime = "emscripten"), "--use-port=emdawnwebgpu");
    mel_link(lib, MEL_PUBLIC, WHEN(.runtime = "emscripten"), "--use-port=emdawnwebgpu", "--pre-js", "modules/gpu/src/webgpu/device_preinit.js");

    mel_depends(lib, "core");
    mel_depends(lib, "reactor");
    mel_depends(lib, "time");
    mel_depends(lib, "webgpu");
}
