#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "string");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "hash");
    mel_depends(lib, "collection");

    Mel_Target* test = mel_add_test(b, "string-builder");
    mel_sources(test, ALWAYS, "test/test_builder.c");
    mel_sources(test, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(test, "test");
    mel_depends(test, "string");
    mel_depends(test, "allocator");
    mel_depends(test, "core");
}
