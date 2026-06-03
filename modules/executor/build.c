#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "executor");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");

    Mel_Target* t = mel_add_test(b, "executor-core");
    mel_sources(t, ALWAYS, "test/test_executor.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "executor");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
}
