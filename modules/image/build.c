#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "image");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "color");
    mel_depends(lib, "debug");
    mel_depends(lib, "log");
    mel_depends(lib, "collection");
    mel_depends(lib, "thread");
    mel_depends(lib, "stb");

    Mel_Target* t = mel_add_test(b, "image-core");
    mel_sources(t, ALWAYS, "test/image_test.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "image");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "color");
    mel_depends(t, "debug");
    mel_depends(t, "log");
    mel_depends(t, "collection");
    mel_depends(t, "thread");
    mel_depends(t, "stb");

    Mel_Target* bridge = mel_add_library(b, "image-gpu");
    mel_includes(bridge, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(bridge, ALWAYS, "src/gpu/*.c");
    mel_depends(bridge, "image");
    mel_depends(bridge, "core");
    mel_depends(bridge, "gpu");

    Mel_Target* gt = mel_add_test(b, "image-gpu-test");
    mel_sources(gt, ALWAYS, "test/gpu_test.c");
    mel_sources(gt, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(gt, "test");
    mel_depends(gt, "image-gpu");
    mel_depends(gt, "image");
    mel_depends(gt, "core");
    mel_depends(gt, "gpu");
}
