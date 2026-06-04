#include "build.h"

void build(Mel_Build* b)
{
    Mel_Target* lib = mel_add_library(b, "stb");
    mel_includes(lib, MEL_PUBLIC, ALWAYS, "stb");
    mel_cflags(lib, MEL_PRIVATE, ALWAYS, "-w");
    mel_sources(lib, ALWAYS, "src/stb_impl.c");
}
