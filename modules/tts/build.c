#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "tts");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");

    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "apple/src/*.m");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "-framework", "AVFoundation", "-framework", "Foundation");

    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "win32/src/*.c");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WIN32)), "-lole32", "-lsapi");

    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX)), "linux/src/*.c");

    mel_sources(lib, WHEN(.platforms = MEL_ON(ANDROID)), "android/src/*.c");
    mel_android_java(lib, "android/java");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(ANDROID)), "-landroid", "-llog");
    mel_depends_when(lib, "platform", WHEN(.platforms = MEL_ON(ANDROID)));

    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "web/src/*.c");

    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "string");
    mel_depends(lib, "log");
    mel_depends(lib, "thread");

    Mel_Target* t = mel_add_test(b, "tts-core");
    mel_includes(t, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(t, ALWAYS, "src/*.c");
    mel_sources(t, ALWAYS, "test/test_tts.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "string");
    mel_depends(t, "log");
}
