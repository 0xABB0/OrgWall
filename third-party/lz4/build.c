#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "lz4");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "lz4");
    mel_cflags(lib, MEL_PRIVATE, ALWAYS, "-w");
    mel_sources(lib, ALWAYS, "lz4/lz4.c", "lz4/lz4hc.c", "lz4/lz4frame.c", "lz4/xxhash.c");
}
