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

    /* The MEL_TEST harness (modules/test) has no runtime or main implemented yet,
     * so the verification is a runnable example that self-checks and dumps a PPM.
     * test/pixmap_test.c is kept for when that harness lands. */
    Mel_Target* ex = mel_add_executable(b, "paint-example");
    mel_sources(ex, ALWAYS, "example/paint_example.c");
    mel_depends(ex, "paint");
    mel_depends(ex, "core");
    mel_depends(ex, "allocator");
    mel_depends(ex, "color");
    mel_depends(ex, "math");
    mel_depends(ex, "string");
}
