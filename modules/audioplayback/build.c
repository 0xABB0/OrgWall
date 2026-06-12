#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "audioplayback");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "audioout");
    mel_depends(lib, "pcm");
    mel_depends(lib, "log");

    Mel_Target* t = mel_add_test(b, "audioplayback-core");
    mel_includes(t, MEL_PUBLIC, ALWAYS, "include");
    mel_includes(t, MEL_PUBLIC, ALWAYS, "../audioout/include");
    mel_sources(t, ALWAYS, "src/audioplayback.c");
    mel_sources(t, ALWAYS, "../audioout/src/audioout.c");
    mel_sources(t, ALWAYS, "../audioout/src/descriptors.c");
    mel_sources(t, ALWAYS, "../audioout/src/publish.c");
    mel_sources(t, ALWAYS, "test/test_audioplayback.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "string");
    mel_depends(t, "executor");
    mel_depends(t, "event");
    mel_depends(t, "log");
    mel_depends(t, "pcm");
}
