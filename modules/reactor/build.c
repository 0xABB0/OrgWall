#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "reactor");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS)), "src/macos/*.m");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "-framework", "Foundation", "-framework", "CoreFoundation");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "AppKit");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(ANDROID)), "-landroid");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "executor");
    mel_depends(lib, "thread");
    mel_depends(lib, "time");

    Mel_Target* t = mel_add_test(b, "reactor-defer");
    mel_sources(t, ALWAYS, "test/test_reactor_defer.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "reactor");
    mel_depends(t, "executor");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "thread");
    mel_depends(t, "time");

    Mel_Target* rt = mel_add_test(b, "reactor-regress");
    mel_sources(rt, ALWAYS, "test/test_reactor_regress.c");
    mel_sources(rt, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(rt, "test");
    mel_depends(rt, "reactor");
    mel_depends(rt, "executor");
    mel_depends(rt, "core");
    mel_depends(rt, "allocator");
    mel_depends(rt, "collection");
    mel_depends(rt, "thread");
    mel_depends(rt, "time");
}
