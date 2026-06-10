#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "await");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "executor");
    mel_depends(lib, "future");
    mel_depends(lib, "channel");
    mel_depends(lib, "signal");
    mel_depends(lib, "vat");
    mel_depends(lib, "time");

    Mel_Target* t = mel_add_test(b, "await-bridge");
    mel_sources(t, ALWAYS, "test/test_await.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "await");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "executor");
    mel_depends(t, "future");
    mel_depends(t, "channel");
    mel_depends(t, "signal");
    mel_depends(t, "job");
    mel_depends(t, "vat");
    mel_depends(t, "thread");
    mel_depends(t, "time");
}
