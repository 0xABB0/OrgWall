#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "process");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/process.c", "src/process_pipe.c", "src/process_run.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS) | MEL_ON(LINUX) | MEL_ON(ANDROID)), "src/posix/process_backend.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/win32/process_backend.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "src/wasm/process_backend.c");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "executor");
    mel_depends(lib, "future");
    mel_depends(lib, "reactor");
    mel_depends(lib, "port");
    mel_depends(lib, "io");
    mel_depends(lib, "log");

    Mel_Target* t = mel_add_test(b, "process-spawn");
    mel_includes(t, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(t, ALWAYS, "test/test_process.c");
    mel_sources(t, ALWAYS, "../../tools/test/src/runner.c");
    mel_depends(t, "test");
    mel_depends(t, "process");
    mel_depends(t, "core");
    mel_depends(t, "allocator");
    mel_depends(t, "collection");
    mel_depends(t, "executor");
    mel_depends(t, "future");
    mel_depends(t, "reactor");
    mel_depends(t, "port");
    mel_depends(t, "io");
    mel_depends(t, "thread");
    mel_depends(t, "time");
    mel_depends(t, "log");
}
