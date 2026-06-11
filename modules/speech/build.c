#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "speech");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/speech.c", "src/descriptors.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "src/apple/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX) | MEL_ON(WIN32) | MEL_ON(ANDROID) | MEL_ON(WASM)), "src/speech_host_none.c");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "-framework", "AVFoundation", "-framework", "Speech", "-framework", "Foundation");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "future");
    mel_depends(lib, "executor");
    mel_depends(lib, "string");
    mel_depends(lib, "log");

    Mel_Target* t = mel_add_test(b, "speech-core");
    mel_includes(t, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(t, ALWAYS, "src/speech.c");
    mel_sources(t, ALWAYS, "src/descriptors.c");
    mel_sources(t, ALWAYS, "test/speech_test.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "future");
    mel_depends(t, "executor");
    mel_depends(t, "string");
    mel_depends(t, "log");
}
