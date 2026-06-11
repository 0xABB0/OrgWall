#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "zstd");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "zstd");
    mel_cflags(lib, MEL_PRIVATE, ALWAYS, "-w");
    mel_defines(lib, MEL_PRIVATE, ALWAYS, "ZSTD_DISABLE_ASM=1");
    mel_sources(lib, ALWAYS, "zstd/common/*.c", "zstd/compress/*.c", "zstd/decompress/*.c");
}
