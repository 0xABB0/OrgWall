#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "tray");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/tray.c", "src/menu.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS)), "macos/src/*.m");
    mel_includes(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "macos/include");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "win32/src/*.c");
    mel_includes(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WIN32)), "win32/include");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX)), "linux/src/*.c");
    mel_includes(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(LINUX)), "linux/include");
    mel_sources(lib, WHEN(.platforms = MEL_ON(IOS) | MEL_ON(ANDROID) | MEL_ON(WASM)), "src/tray_host_none.c");

    mel_cflags(lib, MEL_PRIVATE, WHEN(.platforms = MEL_ON(MACOS)), "-fobjc-arc");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "AppKit", "-framework", "Foundation");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WIN32)), "-lshell32", "-luser32", "-lgdi32");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(LINUX)), "-ldl");

    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "string");
    mel_depends(lib, "event");
    mel_depends(lib, "executor");
    mel_depends(lib, "log");

    Mel_Target* t = mel_add_test(b, "tray-core");
    mel_includes(t, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(t, ALWAYS, "test/tray_test.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_cflags(t, MEL_PRIVATE, WHEN(.platforms = MEL_ON(MACOS)), "-fobjc-arc");
    mel_link(t, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "AppKit", "-framework", "Foundation");
    mel_depends(t, "test");
    mel_depends(t, "tray");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "string");
    mel_depends(t, "event");
    mel_depends(t, "executor");
    mel_depends(t, "log");
}
