#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "allocator");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_depends(lib, "core");

    Mel_Target* track_test = mel_add_test(b, "allocator-tracking");
    mel_sources(track_test, ALWAYS, "test/test.tracking.c");
    mel_sources(track_test, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(track_test, "test");
    mel_depends(track_test, "allocator");
    mel_depends(track_test, "core");
}
