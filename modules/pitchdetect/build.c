#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "pitchdetect");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");

    Mel_Target* t = mel_add_test(b, "pitchdetect-test");
    mel_sources(t, ALWAYS, "test/test_pitchdetect.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "pitchdetect");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
}
