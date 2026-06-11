#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "rng");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS) | MEL_ON(IOS) | MEL_ON(LINUX) | MEL_ON(ANDROID)), "posix/src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "win32/src/*.c");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(WIN32)), "-lbcrypt");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WASM)), "wasm/src/*.c");
    mel_depends(lib, "core");
}
