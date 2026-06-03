#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "job");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/job.c");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "thread");
    mel_depends(lib, "fiber");
    mel_depends(lib, "signal");
    mel_depends(lib, "executor");

    Mel_Target* t = mel_add_test(b, "job-executor");
    mel_sources(t, ALWAYS, "test/test_job.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "job");
    mel_depends(t, "executor");
    mel_depends(t, "signal");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "thread");
}
