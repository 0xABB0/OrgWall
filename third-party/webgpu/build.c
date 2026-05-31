#include "build.h"

#define DAWN_VERSION "7187"

#if defined(__aarch64__) || defined(__arm64__)
#  define DAWN_MACOS_ARCH "aarch64"
#else
#  define DAWN_MACOS_ARCH "x64"
#endif

#define DAWN_PREBUILT_URL                                                      \
    "https://github.com/eliemichel/dawn-prebuilt/releases/download/chromium%2F" \
    DAWN_VERSION "/Dawn-" DAWN_VERSION "-macos-" DAWN_MACOS_ARCH "-Release.zip"

void build(Mel_Build *b) {
    Mel_Target *tp = mel_add_third_party(b, "webgpu");

    mel_prebuilt(tp, WHEN(.platforms = MEL_ON(MACOS), .gpu = "webgpu"), DAWN_PREBUILT_URL,
                 "libwebgpu_dawn.dylib");

    mel_cmake_when(tp, WHEN(.platforms = MEL_ON(ANDROID), .gpu = "webgpu"));
    mel_cmake(tp, "dawn", "-DDAWN_FETCH_DEPENDENCIES=ON", "-DDAWN_BUILD_MONOLITHIC_LIBRARY=SHARED",
              "-DDAWN_ENABLE_INSTALL=ON", "-DDAWN_BUILD_SAMPLES=OFF", "-DTINT_BUILD_TESTS=OFF",
              "-DTINT_BUILD_CMD_TOOLS=OFF", "-DDAWN_ENABLE_DESKTOP_GL=OFF",
              "-DDAWN_ENABLE_OPENGLES=OFF", "-DPython3_EXECUTABLE=/usr/bin/python3");

    mel_link(tp, MEL_PUBLIC, WHEN(.gpu = "webgpu"), "-lwebgpu_dawn");
}
