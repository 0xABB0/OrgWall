#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "dialog");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/dialog.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS)), "src/apple/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(IOS)), "src/ios/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/win32/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX)), "src/linux/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(ANDROID)), "src/android/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "src/web/*.c");
    mel_android_java(lib, "src/android/java");
    mel_android_manifest(lib, "src/android/AndroidManifest.xml");

    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "AppKit", "-framework", "Foundation", "-framework", "UniformTypeIdentifiers");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(IOS)), "-framework", "UIKit", "-framework", "Foundation", "-framework", "UniformTypeIdentifiers");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WIN32)), "-lole32", "-loleaut32", "-lshell32", "-luuid");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(LINUX)), "-ldbus-1", "-ldl");

    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "string");
    mel_depends(lib, "executor");
    mel_depends(lib, "future");
    mel_depends(lib, "event");
    mel_depends(lib, "vat");
    mel_depends(lib, "window");
    mel_depends(lib, "log");
    mel_depends(lib, "platform");

    Mel_Target* t = mel_add_test(b, "dialog-core");
    mel_includes(t, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(t, ALWAYS, "src/dialog.c");
    mel_sources(t, ALWAYS, "test/test_dialog.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "string");
    mel_depends(t, "executor");
    mel_depends(t, "future");
    mel_depends(t, "event");
    mel_depends(t, "vat");
    mel_depends(t, "window");
    mel_depends(t, "log");
    mel_depends(t, "platform");
}
