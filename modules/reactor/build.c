#include "build.h"

void build(Mel_Build *b) {
    Mel_Target *lib = mel_add_library(b, "reactor");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_sources(lib, WHEN(.platforms = MEL_ON(MACOS)), "src/macos/*.m");
    mel_link(lib, MEL_PUBLIC, WHEN(.platforms = MEL_ON(MACOS)), "-framework", "Foundation",
             "-framework", "CoreFoundation");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "collection");
    mel_depends(lib, "thread");
    mel_depends(lib, "time");
}
