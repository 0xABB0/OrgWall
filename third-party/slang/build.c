#include "build.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define getcwd _getcwd
#else
#include <unistd.h>
#endif

#define SLANG_VERSION "2026.10.2"

#define SLANG_VENDOR_ANDROID "tools/build/vendor/slang/slang-" SLANG_VERSION "-android-aarch64.zip"
#define SLANG_VENDOR_IOS_SIM "tools/build/vendor/slang/slang-" SLANG_VERSION "-ios-sim-aarch64.zip"
#define SLANG_VENDOR_WASM_MT "tools/build/vendor/slang/slang-" SLANG_VERSION "-wasm-mt.zip"

#define SLANG_OS_ARCH_URL(os, arch) \
    SLANG_REL_BASE "slang-" SLANG_VERSION "-" os "-" arch ".zip"

static char* mel_slang__vendor_url(const char* rel, const char* script)
{
    char cwd[4096];
    if (!getcwd(cwd, sizeof cwd))
        return NULL;
    size_t n = strlen("file://") + strlen(cwd) + 1 + strlen(rel) + 1;
    char*  s = malloc(n);
    snprintf(s, n, "file://%s/%s", cwd, rel);
    struct stat st;
    if (stat(rel, &st) != 0)
        fprintf(stderr,
                "build: slang prebuilt missing at %s\n"
                "build: produce it with tools/build/vendor/slang/%s\n",
                rel,
                script);
    return s;
}

#define SLANG_REL_BASE \
    "https://github.com/shader-slang/slang/releases/download/v" SLANG_VERSION "/"

void build(Mel_Build* b)
{
    Mel_Target* rt = mel_add_third_party(b, "slang-runtime");
    mel_prebuilt(rt, WHEN(.platforms = MEL_ON(MACOS), .arch = "arm64"), SLANG_OS_ARCH_URL("macos", "aarch64"), "libslang.dylib");
    mel_prebuilt(rt, WHEN(.platforms = MEL_ON(MACOS), .arch = "x86_64"), SLANG_OS_ARCH_URL("macos", "x86_64"), "libslang.dylib");
    mel_prebuilt(rt, WHEN(.platforms = MEL_ON(LINUX), .arch = "arm64"), SLANG_OS_ARCH_URL("linux", "aarch64"), "libslang.so");
    mel_prebuilt(rt, WHEN(.platforms = MEL_ON(LINUX), .arch = "x86_64"), SLANG_OS_ARCH_URL("linux", "x86_64"), "libslang.so");
    mel_prebuilt(rt, WHEN(.platforms = MEL_ON(WIN32), .arch = "arm64"), SLANG_OS_ARCH_URL("windows", "aarch64"), "slang.lib");
    mel_prebuilt(rt, WHEN(.platforms = MEL_ON(WIN32), .arch = "x86_64"), SLANG_OS_ARCH_URL("windows", "x86_64"), "slang.lib");
    mel_link(rt, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(LINUX) | MEL_ON(WIN32)), "-lslang");

    Mel_Target* wasm = mel_add_third_party(b, "slang-wasm");
    mel_prebuilt(wasm, WHEN(.platforms = MEL_ON(WASM)), mel_slang__vendor_url(SLANG_VENDOR_WASM_MT, "build-wasm-mt.sh"), "libslang-compiler.a");
    mel_link(wasm, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WASM)), "-fwasm-exceptions");
    mel_link(wasm, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WASM)), "-Wl,--start-group");
    mel_link(wasm, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WASM)), "-lslang-compiler");
    mel_link(wasm, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WASM)), "-lcompiler-core");
    mel_link(wasm, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WASM)), "-lcore");
    mel_link(wasm, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WASM)), "-lcmark-gfm");
    mel_link(wasm, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WASM)), "-lminiz");
    mel_link(wasm, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WASM)), "-llz4");
    mel_link(wasm, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WASM)), "-Wl,--end-group");

    Mel_Target* droid = mel_add_third_party(b, "slang-android");
    mel_prebuilt(droid, WHEN(.platforms = MEL_ON(ANDROID)), mel_slang__vendor_url(SLANG_VENDOR_ANDROID, "build-android-arm64.sh"), "libslang-compiler.so");
    mel_link(droid, MEL_PUBLIC, WHEN(.platforms = MEL_ON(ANDROID)), "-lslang-compiler");

    Mel_Target* ios = mel_add_third_party(b, "slang-ios");
    mel_prebuilt(ios, WHEN(.platforms = MEL_ON(IOS)), mel_slang__vendor_url(SLANG_VENDOR_IOS_SIM, "build-ios-sim-arm64.sh"), "libslang-compiler.dylib");
    mel_link(ios, MEL_PUBLIC, WHEN(.platforms = MEL_ON(IOS)), "-lslang-compiler");
    mel_link(ios, MEL_PUBLIC, WHEN(.platforms = MEL_ON(IOS)), "-Wl,-rpath,@executable_path/Frameworks");

    Mel_Target* lib = mel_add_library(b, "slang");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.cpp");
    mel_cflags(lib, MEL_PRIVATE, WHEN(.platforms = MEL_ON(WASM)), "-fwasm-exceptions", "-pthread");
    mel_depends_when(lib, "slang-runtime", WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(LINUX) | MEL_ON(WIN32)));
    mel_depends_when(lib, "slang-wasm", WHEN(.platforms = MEL_ON(WASM)));
    mel_depends_when(lib, "slang-android", WHEN(.platforms = MEL_ON(ANDROID)));
    mel_depends_when(lib, "slang-ios", WHEN(.platforms = MEL_ON(IOS)));
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(LINUX) | MEL_ON(IOS)), "-lc++");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(ANDROID)), "-lc++_static", "-lc++abi");

    mel_defines(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WIN32)), "MEL_SLANG_EMIT_DXIL=1");

    Mel_Target* t = mel_add_test(b, "slang-compile");
    mel_sources(t, ALWAYS, "test/compile_test.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "slang");
    mel_depends(t, "test");
    mel_depends(t, "core");
}
