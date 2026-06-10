#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "fs");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/fs.c", "src/glob.c", "src/paths.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "src/apple/fs_apple.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX)), "src/linux/fs_linux.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(ANDROID)), "src/android/fs_android.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/win32/fs_win32.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "src/wasm/fs_wasm.c");

    mel_cflags(lib, MEL_PRIVATE, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "-fobjc-arc");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "-framework", "Foundation");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WIN32)), "-lshell32", "-lole32");

    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "string");
    mel_depends(lib, "executor");
    mel_depends(lib, "future");
    mel_depends(lib, "vat");
    mel_depends(lib, "thread");
    mel_depends(lib, "log");
    mel_depends(lib, "platform");

    Mel_Target* t = mel_add_test(b, "fs-core");
    mel_includes(t, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(t, ALWAYS, "test/fs_test.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_cflags(t, MEL_PRIVATE, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "-fobjc-arc");
    mel_link(t, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "-framework", "Foundation");
    mel_depends(t, "test");
    mel_depends(t, "fs");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "string");
    mel_depends(t, "executor");
    mel_depends(t, "future");
    mel_depends(t, "vat");
    mel_depends(t, "thread");
    mel_depends(t, "time");
    mel_depends(t, "log");
}
