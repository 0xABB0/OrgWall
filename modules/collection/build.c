#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "collection");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "hash");
    mel_depends(lib, "thread");

    Mel_Target* slotmap_test = mel_add_test(b, "collection-slotmap");
    mel_sources(slotmap_test, ALWAYS, "test/test_slotmap.c");
    mel_sources(slotmap_test, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(slotmap_test, "test");
    mel_depends(slotmap_test, "collection");
    mel_depends(slotmap_test, "core");
    mel_depends(slotmap_test, "allocator");
}
