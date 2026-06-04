#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "platform");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/platform.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS)), "src/macos/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(IOS)), "src/ios/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX)), "src/linux/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/win32/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(ANDROID)), "src/android/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "src/web/*.c");
    mel_android_java(lib, "src/android/java");
    mel_android_manifest(lib, "src/android/AndroidManifest.xml");

    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "IOKit", "-framework", "CoreFoundation");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(IOS)), "-framework", "UIKit", "-framework", "QuartzCore", "-framework", "Foundation");

    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "future");
    mel_depends(lib, "executor");

    Mel_Target* t = mel_add_test(b, "platform-core");
    mel_includes(t, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(t, ALWAYS, "src/platform.c");
    mel_sources(t, WHEN(.platforms = MEL_ON(MACOS)), "src/macos/*.c");
    mel_sources(t, WHEN(.platforms = MEL_ON(IOS)), "src/ios/*.m");
    mel_sources(t, WHEN(.platforms = MEL_ON(LINUX)), "src/linux/*.c");
    mel_sources(t, WHEN(.platforms = MEL_ON(WIN32)), "src/win32/*.c");
    mel_sources(t, WHEN(.platforms = MEL_ON(ANDROID)), "src/android/*.c");
    mel_sources(t, WHEN(.platforms = MEL_ON(WASM)), "src/web/*.c");
    mel_sources(t, ALWAYS, "test/test_platform.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_link(t, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "IOKit", "-framework", "CoreFoundation");
    mel_link(t, MEL_PUBLIC, WHEN(.platforms = MEL_ON(IOS)), "-framework", "UIKit", "-framework", "QuartzCore", "-framework", "Foundation");
    mel_depends(t, "test");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "future");
    mel_depends(t, "executor");
}
