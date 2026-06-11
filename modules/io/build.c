#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "io");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/stream.c", "src/memory.c", "src/file.c", "src/whole_file.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS) | MEL_ON(LINUX) | MEL_ON(ANDROID)), "posix/src/file_backend.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "win32/src/file_backend.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "wasm/src/file_backend.c");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "executor");
    mel_depends(lib, "future");
    mel_depends(lib, "vat");
    mel_depends(lib, "port");
    mel_depends(lib, "log");

    Mel_Target* t = mel_add_test(b, "io-stream");
    mel_includes(t, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(t, ALWAYS, "test/test_io.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "io");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "executor");
    mel_depends(t, "future");
    mel_depends(t, "vat");
    mel_depends(t, "port");
    mel_depends(t, "thread");
    mel_depends(t, "time");
    mel_depends(t, "log");
}
