#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "debug");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS)), "src/macos/*.c", "src/macos/*.m", "src/posix/assert_backend.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(IOS)), "src/ios/*.c", "src/posix/assert_backend.c", "src/nodialog/assert_dialog.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/windows/*.c");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "AppKit", "-framework", "Foundation");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WIN32)), "-ldbghelp");
    mel_sources(lib, WHEN(.platforms = MEL_ON(ANDROID)), "src/android/*.c", "src/posix/assert_backend.c", "src/nodialog/assert_dialog.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX)), "src/linux/*.c", "src/posix/assert_backend.c", "src/nodialog/assert_dialog.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "src/wasm/*.c", "src/nodialog/assert_dialog.c");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "platform");
    mel_depends(lib, "string");

    Mel_Target* t = mel_add_test(b, "debug-assert");
    mel_sources(t, ALWAYS, "test/test_assert.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "debug");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "platform");
    mel_depends(t, "string");
}
