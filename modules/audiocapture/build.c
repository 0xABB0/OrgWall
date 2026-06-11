#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "audiocapture");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS)), "macos/src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS)), "macos/src/*.m");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "AudioToolbox", "-framework", "CoreAudio", "-framework", "CoreFoundation", "-framework", "AVFoundation", "-framework", "Foundation");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "string");

    Mel_Target* t = mel_add_test(b, "audiocapture-test");
    mel_sources(t, ALWAYS, "test/test_audiocapture.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "audiocapture");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
}
