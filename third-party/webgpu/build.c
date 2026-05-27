#include "build.h"

#include <string.h>

// Dawn (Google's native WebGPU). macOS consumes eliemichel's prebuilt release
// archive; Android has no published prebuilt, so it is built from source with
// the NDK toolchain mel_tp_cmake already supplies. The same chromium milestone
// is pinned on both so the webgpu.h ABI matches across targets.
#define DAWN_VERSION "7187"

#if defined(__aarch64__) || defined(__arm64__)
#  define DAWN_MACOS_ARCH "aarch64"
#else
#  define DAWN_MACOS_ARCH "x64"
#endif

#define DAWN_PREBUILT_URL \
    "https://github.com/eliemichel/dawn-prebuilt/releases/download/chromium%2F" \
    DAWN_VERSION "/Dawn-" DAWN_VERSION "-macos-" DAWN_MACOS_ARCH "-Release.zip"

#define DAWN_SOURCE_REPO "https://dawn.googlesource.com/dawn"
#define DAWN_SOURCE_DIR  "third-party/webgpu/dawn"

static const char *const k_dawn_cmake_args[] = {
    "-DDAWN_FETCH_DEPENDENCIES=ON",
    "-DDAWN_BUILD_MONOLITHIC_LIBRARY=SHARED",
    "-DDAWN_ENABLE_INSTALL=ON",
    "-DDAWN_BUILD_SAMPLES=OFF",
    "-DTINT_BUILD_TESTS=OFF",
    "-DTINT_BUILD_CMD_TOOLS=OFF",
    // Android uses the Vulkan-backed Dawn; the GL backends drag in an OpenGL
    // loader generator we don't want and that trips on some hosts' Python.
    "-DDAWN_ENABLE_DESKTOP_GL=OFF",
    "-DDAWN_ENABLE_OPENGLES=OFF",
    // Dawn's code generators parse XML through Python's expat; pin the macOS
    // system interpreter, whose stdlib ships a working pyexpat (Homebrew's
    // python3 here does not).
    "-DPython3_EXECUTABLE=/usr/bin/python3",
};

static bool build_webgpu(Mel_Build_Context *ctx) {
    const char *gpu = mel_build_ctx_gpu_backend(ctx);
    if (!gpu || strcmp(gpu, "webgpu") != 0) return true;

    if (mel_build_ctx_platform(ctx) == MEL_PLATFORM_ANDROID) {
        if (!mel_tp_fetch_git(DAWN_SOURCE_REPO, "chromium/" DAWN_VERSION, DAWN_SOURCE_DIR)) return false;
        return mel_tp_cmake(ctx, DAWN_SOURCE_DIR, k_dawn_cmake_args,
                            sizeof(k_dawn_cmake_args) / sizeof(k_dawn_cmake_args[0]),
                            "libwebgpu_dawn.so");
    }

    return mel_tp_fetch_prebuilt(ctx, DAWN_PREBUILT_URL, "libwebgpu_dawn.dylib");
}

bool project(Mel_Build_Target *t) {
    mel_build_set_name(t, "webgpu");
    mel_build_set_kind(t, MEL_TARGET_THIRD_PARTY);
    static const Mel_Platform platforms[] = { MEL_PLATFORM_MACOS, MEL_PLATFORM_ANDROID };
    mel_build_set_platforms(t, platforms, 2);

    // Only the webgpu gpu axis pulls Dawn into the link; metal/vulkan builds add
    // the prefix's -L/-I (harmless, empty) but never the library itself.
    mel_build_add_link_flag_on_gpu(t, MEL_PUBLIC, "webgpu", "-lwebgpu_dawn");

    mel_build_suppress_default(t, MEL_STAGE_COMPILE);
    mel_build_suppress_default(t, MEL_STAGE_LINK);
    mel_build_on_compile(t, build_webgpu);
    return true;
}
