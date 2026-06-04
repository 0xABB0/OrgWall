#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "dylib");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/dylib.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS) | MEL_ON(LINUX) | MEL_ON(ANDROID)), "src/posix/dylib_posix.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/win32/dylib_win32.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "src/wasm/dylib_wasm.c");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(LINUX) | MEL_ON(ANDROID)), "-ldl");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "log");

    Mel_Target* t = mel_add_test(b, "dylib-core");
    mel_includes(t, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(t, ALWAYS, "test/dylib_test.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "dylib");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "log");
}
