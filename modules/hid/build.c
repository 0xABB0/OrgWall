#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "hid");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/hid.c", "src/events.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS)), "macos/src/*.m");
    mel_includes(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "macos/include");
    mel_sources(lib, WHEN(.platforms = MEL_ON(IOS)), "ios/src/*.m");
    mel_includes(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(IOS)), "ios/include");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX)), "linux/src/*.c");
    mel_includes(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(LINUX)), "linux/include");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "win32/src/*.c");
    mel_includes(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WIN32)), "win32/include");
    mel_sources(lib, WHEN(.platforms = MEL_ON(ANDROID)), "android/src/*.c");
    mel_includes(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(ANDROID)), "android/include");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "wasm/src/*.c");
    mel_includes(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WASM)), "wasm/include");
    mel_android_java(lib, "android/java");

    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "IOKit", "-framework", "CoreFoundation");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(LINUX)), "-ludev");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WIN32)), "-lhid", "-lsetupapi");

    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "event");
    mel_depends(lib, "executor");
    mel_depends(lib, "future");
    mel_depends(lib, "port");
    mel_depends(lib, "vat");
    mel_depends(lib, "log");
    mel_depends(lib, "platform");

    Mel_Target* t = mel_add_test(b, "hid-core");
    mel_sources(t, ALWAYS, "test/hid_test.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "hid");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "event");
    mel_depends(t, "executor");
    mel_depends(t, "future");
    mel_depends(t, "port");
    mel_depends(t, "vat");
    mel_depends(t, "log");
}
