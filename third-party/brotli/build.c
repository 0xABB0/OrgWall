#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "brotli");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "include");
    mel_cflags(lib, MEL_PRIVATE, ALWAYS, "-w");
    mel_sources(lib, ALWAYS, "brotli/common/*.c", "brotli/dec/*.c", "brotli/enc/*.c");
}
