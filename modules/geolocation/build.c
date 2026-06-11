#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "geolocation");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/geolocation.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "apple/src/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(ANDROID)), "android/src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX)), "linux/src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "win32/src/*.c", "win32/src/*.cpp");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "wasm/src/*.c");

    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "-framework", "CoreLocation", "-framework", "Foundation");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WIN32)), "-lwindowsapp", "-lole32");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(LINUX)), "-ldl");

    mel_android_manifest(lib, "android/AndroidManifest.xml");
    mel_android_java(lib, "android/java");
    mel_android_dependency(lib, "com.google.android.gms:play-services-location:21.3.0");

    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "string");
    mel_depends(lib, "future");
    mel_depends(lib, "executor");
    mel_depends(lib, "vat");
    mel_depends(lib, "time");
    mel_depends(lib, "log");
    mel_depends(lib, "debug");
    mel_depends_when(lib, "platform", WHEN(.platforms = MEL_ON(ANDROID)));

    Mel_Target* t = mel_add_test(b, "geolocation-core");
    mel_includes(t, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(t, ALWAYS, "src/geolocation.c", "test/mock_provider.c", "test/geolocation_test.c", "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "string");
    mel_depends(t, "future");
    mel_depends(t, "executor");
    mel_depends(t, "vat");
    mel_depends(t, "time");
    mel_depends(t, "log");
    mel_depends(t, "debug");
    mel_depends(t, "thread");
}
