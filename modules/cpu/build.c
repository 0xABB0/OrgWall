#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "cpu");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "src/apple/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX) | MEL_ON(ANDROID)), "src/linux/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/win32/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "src/web/*.c");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WIN32)), "-ladvapi32");
    mel_depends(lib, "core");

    Mel_Target* t = mel_add_test(b, "cpu-test");
    mel_sources(t, ALWAYS, "test/cpu_test.c");
    mel_depends(t, "cpu");
    mel_depends(t, "core");
}
