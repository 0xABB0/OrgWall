#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "thread");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_depends(lib, "core");

    mel_sources(lib, ALWAYS, "src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "src/apple/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS)), "src/posix/mutex.c", "src/posix/rwlock.c", "src/posix/tls.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(LINUX) | MEL_ON(ANDROID)), "src/posix/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/win32/*.c");

    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "src/posix/*.c");
    mel_cflags(lib, MEL_PRIVATE, WHEN(.platforms = MEL_ON(WASM)), "-D_GNU_SOURCE");
    mel_cflags(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WASM)), "-pthread");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WASM)), "-pthread", "-sWASM_WORKERS=1");
}
