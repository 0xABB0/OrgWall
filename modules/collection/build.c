#include "build.h"

void build(Mel_Build *b) {
    Mel_Target *lib = mel_add_library(b, "collection");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_sources(lib, ALWAYS, "src/*.c");
    mel_depends(lib, "core");
    mel_depends(lib, "allocator");
    mel_depends(lib, "hash");
    mel_depends(lib, "thread");
}
