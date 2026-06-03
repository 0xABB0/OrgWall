#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "channel");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "executor");
    mel_depends(lib, "future");
    mel_depends(lib, "signal");
    mel_depends(lib, "thread");

    Mel_Target* t = mel_add_test(b, "channel-core");
    mel_sources(t, ALWAYS, "test/test_channel.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "channel");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "executor");
    mel_depends(t, "future");
    mel_depends(t, "signal");
    mel_depends(t, "thread");

    Mel_Target* tr = mel_add_test(b, "channel-race");
    mel_sources(tr, ALWAYS, "test/test_channel_race.c");
    mel_sources(tr, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(tr, "test");
    mel_depends(tr, "channel");
    mel_depends(tr, "core");
    mel_depends(tr, "allocator");
    mel_depends(tr, "collection");
    mel_depends(tr, "executor");
    mel_depends(tr, "future");
    mel_depends(tr, "signal");
    mel_depends(tr, "thread");

    Mel_Target* tj = mel_add_test(b, "channel-fiber");
    mel_sources(tj, ALWAYS, "test/test_channel_fiber.c");
    mel_sources(tj, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(tj, "test");
    mel_depends(tj, "channel");
    mel_depends(tj, "core");
    mel_depends(tj, "allocator");
    mel_depends(tj, "collection");
    mel_depends(tj, "executor");
    mel_depends(tj, "future");
    mel_depends(tj, "signal");
    mel_depends(tj, "thread");
    mel_depends(tj, "job");
}
