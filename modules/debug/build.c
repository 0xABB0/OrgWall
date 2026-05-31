#include "build.h"

void build(Mel_Build *b) {
    Mel_Target *lib = mel_add_library(b, "debug");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS)), "src/macos/*.c", "src/macos/*.m");
    mel_sources(lib, WHEN(.platforms = MEL_ON(WIN32)), "src/windows/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(ANDROID)), "src/android/*.c");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "platform");
    mel_depends(lib, "string");
}
