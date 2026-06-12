#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "pcm");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");

    Mel_Target* ring = mel_add_test(b, "pcm-ring");
    mel_sources(ring, ALWAYS, "test/test_ring.c");
    mel_sources(ring, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(ring, "test");
    mel_depends(ring, "pcm");
    mel_depends(ring, "core");
    mel_depends(ring, "allocator");
    mel_depends(ring, "thread");

    Mel_Target* resample = mel_add_test(b, "pcm-resample");
    mel_sources(resample, ALWAYS, "test/test_resample.c");
    mel_sources(resample, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(resample, "test");
    mel_depends(resample, "pcm");
    mel_depends(resample, "core");

    Mel_Target* convert = mel_add_test(b, "pcm-convert");
    mel_sources(convert, ALWAYS, "test/test_convert.c");
    mel_sources(convert, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(convert, "test");
    mel_depends(convert, "pcm");
    mel_depends(convert, "core");
}
