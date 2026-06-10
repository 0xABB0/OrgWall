#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "storage");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/storage.c", "src/backend_fs.c", "src/backend_title.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS) | MEL_ON(LINUX) | MEL_ON(ANDROID)), "src/posix/space_posix.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/win32/space_win32.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "src/wasm/space_wasm.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS) | MEL_ON(LINUX) | MEL_ON(WIN32) | MEL_ON(WASM)), "src/title_none.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(ANDROID)), "src/android/title_android.c");

    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(ANDROID)), "-landroid");

    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "string");
    mel_depends(lib, "executor");
    mel_depends(lib, "future");
    mel_depends(lib, "vat");
    mel_depends(lib, "thread");
    mel_depends(lib, "log");
    mel_depends(lib, "fs");
    mel_depends(lib, "io");

    Mel_Target* t = mel_add_test(b, "storage-core");
    mel_includes(t, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(t, ALWAYS, "test/storage_test.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "storage");
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
    mel_depends(t, "fs");
    mel_depends(t, "io");
}
