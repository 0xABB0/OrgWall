#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "audioout");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/audioout.c");
    mel_sources(lib, ALWAYS, "src/descriptors.c");
    mel_sources(lib, ALWAYS, "src/publish.c");

    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS)), "macos/src/*.c");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "CoreAudio", "-framework", "AudioToolbox", "-framework", "CoreFoundation");

    mel_sources(lib, WHEN(.platforms = MEL_ON(IOS)), "ios/src/*.m");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(IOS)), "-framework", "AVFoundation", "-framework", "Foundation");

    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "win32/src/*.c");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WIN32)), "-lole32", "-lksuser");

    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX)), "linux/src/*.c");
    mel_includes(lib, MEL_PRIVATE, WHEN(.platforms = MEL_ON(LINUX)), "../../third-party/alsa/include");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(LINUX)), "-Lthird-party/alsa/lib", "-lasound", "-Wl,--allow-shlib-undefined");

    mel_sources(lib, WHEN(.platforms = MEL_ON(ANDROID)), "android/src/*.c");
    mel_android_java(lib, "android/java");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(ANDROID)), "-laaudio", "-llog");
    mel_depends_when(lib, "platform", WHEN(.platforms = MEL_ON(ANDROID)));

    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "web/src/*.c");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WASM)), "-sAUDIO_WORKLET=1", "-sWASM_WORKERS=1");

    mel_depends(lib, "thread");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "string");
    mel_depends(lib, "executor");
    mel_depends(lib, "event");
    mel_depends(lib, "log");

    Mel_Target* t = mel_add_test(b, "audioout-core");
    mel_includes(t, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(t, ALWAYS, "src/audioout.c");
    mel_sources(t, ALWAYS, "src/descriptors.c");
    mel_sources(t, ALWAYS, "src/publish.c");
    mel_sources(t, ALWAYS, "test/test_audioout.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "string");
    mel_depends(t, "executor");
    mel_depends(t, "event");
    mel_depends(t, "log");
}
