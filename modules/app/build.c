#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "app");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");

    mel_sources(lib, ALWAYS, "src/lifecycle.c");

    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(LINUX)), "src/posix/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS)), "src/macos/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX)), "src/linux/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/win32/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(IOS)), "src/ios/*.c", "src/ios/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(ANDROID)), "src/android/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "src/web/*.c");

    mel_whole_archive(lib, WHEN(.platforms = MEL_ON(ANDROID)));
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "AppKit", "-framework", "Foundation");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(IOS)), "-framework", "UIKit", "-framework", "Foundation");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WASM)), "-lhtml5");

    mel_depends(lib, "core");
    mel_depends(lib, "gui");
    mel_depends(lib, "reactor");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "event");
    mel_depends(lib, "executor");
    mel_depends(lib, "thread");
    mel_depends(lib, "time");
    mel_depends(lib, "log");
    mel_depends(lib, "platform");

    Mel_Target* t = mel_add_test(b, "app-lifecycle");
    mel_includes(t, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(t, ALWAYS, "src/lifecycle.c");
    mel_sources(t, ALWAYS, "test/test_lifecycle.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "core");
    mel_depends(t, "reactor");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "event");
    mel_depends(t, "executor");
    mel_depends(t, "thread");
    mel_depends(t, "time");
    mel_depends(t, "log");
    mel_depends(t, "platform");
}
