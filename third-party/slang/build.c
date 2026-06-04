#include "build.h"

#define SLANG_VERSION "2026.10.2"

#if defined(__aarch64__) || defined(__arm64__)
#  define SLANG_ARCH "aarch64"
#else
#  define SLANG_ARCH "x86_64"
#endif

#define SLANG_URL(os)                                                       \
    "https://github.com/shader-slang/slang/releases/download/v" SLANG_VERSION \
    "/slang-" SLANG_VERSION "-" os "-" SLANG_ARCH ".zip"

void build(Mel_Build* b)
{
    // Vendored libslang, fetched per-host by the build's prebuilt mechanism (no committed binary).
    Mel_Target* rt = mel_add_third_party(b, "slang-runtime");
#if defined(__APPLE__)
    mel_prebuilt(rt, WHEN(.platforms = MEL_ON(MACOS)), SLANG_URL("macos"), "libslang.dylib");
#elif defined(_WIN32)
    mel_prebuilt(rt, WHEN(.platforms = MEL_ON(WIN32)), SLANG_URL("windows"), "slang.lib");
#elif defined(__linux__)
    mel_prebuilt(rt, WHEN(.platforms = MEL_ON(LINUX)), SLANG_URL("linux"), "libslang.so");
#endif
    mel_link(rt, MEL_PUBLIC, ALWAYS, "-lslang");

    // Our C wrapper over libslang's modern C++ API; the engine consumes only this C surface.
    Mel_Target* lib = mel_add_library(b, "slang");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.cpp");
    mel_depends(lib, "slang-runtime");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(LINUX)), "-lc++");

    // Emit-target gating: DXIL needs D3D12 + the dxcompiler/dxil signers, which exist only on win32.
    // Off-win32 the DXIL codegen path is compiled out (loud-fail) so it dead-strips. Public so
    // dependents can gate DXIL requests on the same condition.
    mel_defines(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WIN32)), "MEL_SLANG_EMIT_DXIL=1");

    Mel_Target* t = mel_add_test(b, "slang-compile");
    mel_sources(t, ALWAYS, "test/compile_test.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "slang");
    mel_depends(t, "test");
    mel_depends(t, "core");
}
