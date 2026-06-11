#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "cpu");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/cpu_features.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "apple/src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX) | MEL_ON(ANDROID)), "linux/src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "win32/src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "web/src/*.c");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WIN32)), "-ladvapi32");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");

    Mel_Target* t = mel_add_test(b, "cpu-test");
    mel_sources(t, ALWAYS, "test/cpu_test.c");
    mel_depends(t, "cpu");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
}
