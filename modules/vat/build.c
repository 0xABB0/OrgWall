#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "vat");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "darwin/src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS)), "macos/src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX)), "linux/src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "win32/src/*.c");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "Cocoa", "-framework", "CoreVideo");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "executor");
    mel_depends(lib, "thread");
    mel_depends(lib, "time");

    Mel_Target* t = mel_add_test(b, "vat-core");
    mel_sources(t, ALWAYS, "test/test_vat.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "vat");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "executor");
    mel_depends(t, "thread");
    mel_depends(t, "time");

    Mel_Target* bench = mel_add_executable(b, "vat-bench");
    mel_sources(bench, ALWAYS, "bench/vat_bench.c");
    mel_depends(bench, "vat");
    mel_depends(bench, "core");
    mel_depends(bench, "allocator");
    mel_depends(bench, "collection");
    mel_depends(bench, "executor");
    mel_depends(bench, "thread");
    mel_depends(bench, "time");
}
