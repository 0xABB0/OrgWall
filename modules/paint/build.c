#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "paint");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "src/quartz/*.c");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "-framework", "CoreGraphics", "-framework", "CoreText", "-framework", "CoreFoundation");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "math");
    mel_depends(lib, "string");
    mel_depends(lib, "color");
    mel_depends(lib, "debug");

    Mel_Target* ex = mel_add_executable(b, "paint-example");
    mel_sources(ex, ALWAYS, "example/paint_example.c");
    mel_depends(ex, "paint");
    mel_depends(ex, "core");
    mel_depends(ex, "allocator");
    mel_depends(ex, "color");
    mel_depends(ex, "math");
    mel_depends(ex, "string");

    Mel_Target* t = mel_add_test(b, "paint-pixmap");
    mel_sources(t, ALWAYS, "test/pixmap_test.c");
    /* mel_add_test does not auto-link the harness runtime/main; pull the runner
     * in explicitly. Drop this line if the build ever links it for is_test. */
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "paint");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "color");
    mel_depends(t, "math");
    mel_depends(t, "string");
}
