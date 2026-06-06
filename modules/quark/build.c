#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "quark");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "string");

    Mel_Target* test = mel_add_test(b, "quark-intern");
    mel_sources(test, ALWAYS, "test/test_quark.c");
    mel_sources(test, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(test, "test");
    mel_depends(test, "quark");
    mel_depends(test, "core");
    mel_depends(test, "allocator");
    mel_depends(test, "string");
}
