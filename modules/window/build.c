#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "window");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_sources(lib, WHEN(.backend = "cocoa"), "src/cocoa/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/win32/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(IOS) | MEL_ON(ANDROID) | MEL_ON(LINUX) | MEL_ON(WASM)), "src/stub/*.c");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "Cocoa", "-framework", "QuartzCore");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WIN32)), "-luser32", "-lgdi32", "-lole32", "-lmscms");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "reactor");
    mel_depends(lib, "string");
    mel_depends(lib, "future");
    mel_depends(lib, "executor");
    mel_depends(lib, "debug");

    Mel_Target* t = mel_add_test(b, "window-state");
    mel_sources(t, ALWAYS, "test/state_test.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_link(t, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "Cocoa", "-framework", "QuartzCore");
    mel_depends(t, "test");
    mel_depends(t, "window");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "reactor");
    mel_depends(t, "string");
    mel_depends(t, "future");
    mel_depends(t, "executor");
    mel_depends(t, "debug");
}
