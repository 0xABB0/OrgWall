#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "clipboard");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/clipboard.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "src/apple/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/win32/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(ANDROID)), "src/android/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "src/web/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX)), "src/clipboard_host_none.c");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "AppKit", "-framework", "Foundation");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(IOS)), "-framework", "UIKit", "-framework", "Foundation");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "string");
    mel_depends(lib, "reactor");
    mel_depends(lib, "log");
    mel_depends(lib, "platform");

    // The test compiles the core directly with its own in-memory platform layer
    // (test.clipboard.c implements mel_clip__plat_*), so it must not link the library's
    // host backend — that would duplicate those symbols.
    Mel_Target* t = mel_add_test(b, "clipboard-core");
    mel_includes(t, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(t, ALWAYS, "src/clipboard.c");
    mel_sources(t, ALWAYS, "test/test.clipboard.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_link(t, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "AppKit");
    mel_depends(t, "test");
    mel_depends(t, "core");
    mel_depends(t, "string");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "reactor");
    mel_depends(t, "log");
}
